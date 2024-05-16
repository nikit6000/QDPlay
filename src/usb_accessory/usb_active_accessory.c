#include <pthread.h>
#include <glib.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "messages/v2/message_common_header_v2.h"
#include "messages/v1/app_status.h"
#include "usb_accessory/message_processor/v1/message_processor_v1.h"
#include "usb_accessory/message_processor/v2/message_processor_v2.h"
#include "usb_accessory/usb_active_accessory.h"
#include "usb_accessory/usb_accessory.h"
#include "services/video_receiver/video_receiver.h"
#include "logging/logging.h"

#pragma mark - Private definitions

#define USB_ACTIVE_ACCESSORY_CHUNK_SIZE_512                     (512)
#define USB_ACTIVE_ACCESSORY_CHUNK_SIZE_512_LOG2                (9)
#define USB_ACTIVE_ACCESSORY_WRITE_HB_INTERVAL_SEC              (3)
#define USB_ACTIVE_ACCESSORY_READ_HB_INTERVAL_SEC               (5)
#define USB_ACTIVE_ACCESSORY_HB_CHECK_PERIOD_US 			    (1000)
#define USB_ACTIVE_ACCESSORY_PROBE_PRERIOD_US                   (10000)
#define USB_ACTIVE_ACCESSORY_WORKER_SEC_TO_US(x)                (1000000 * (x))

#pragma mark - Private types

typedef struct {
    int accessory_fd;
    unsigned long last_write_ts;
    unsigned long last_read_ts;
    gboolean is_accessory_connected;
    pthread_mutex_t mutex;
    pthread_mutex_t read_mutex;
    pthread_mutex_t write_mutex;
    pthread_mutex_t processor_mutex;
    const message_processor_t* processor;
} usb_active_accessory_state_t;

const gchar* usb_active_accessory_log_tag = "Active accessory";

#pragma mark - Private properties

usb_active_accessory_state_t usb_active_accessory_state = {
    .accessory_fd = -1,
    .last_write_ts = 0,
    .last_read_ts = 0,
    .is_accessory_connected = FALSE,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .read_mutex = PTHREAD_MUTEX_INITIALIZER,
    .write_mutex = PTHREAD_MUTEX_INITIALIZER,
    .processor_mutex = PTHREAD_MUTEX_INITIALIZER,
    .processor = NULL
};

#pragma mark - Private function definitions

unsigned long                   usb_active_accessory_get_ts(void);
void*                           usb_active_accessory_read_hb(void *arg);
void*                           usb_active_accessory_write_hb(void *arg);
void*                           usb_active_accessory_processor(void *arg);
void                            usb_active_accessory_send_heartbeat(void);
gboolean                        usb_active_accessory_probe(void);

#pragma mark - Internal function implementations

int usb_active_accessory_create_and_wait(const char* device_path) {
    int accessory_fd = -1;
    int ret = -EALREADY;
    pthread_t write_hb_thread = -1, read_hb_thread = -1, processing_thread = -1;

    pthread_mutex_lock(&usb_active_accessory_state.mutex);

    if (usb_active_accessory_state.is_accessory_connected) {
        pthread_mutex_unlock(&usb_active_accessory_state.mutex);

        return ret;
    }

    ret = usb_accessory_configure();

    if (ret < 0) {
        pthread_mutex_unlock(&usb_active_accessory_state.mutex);

        return ret;
    }

    accessory_fd = open(device_path, O_RDWR);

    if (accessory_fd < 0) {
        pthread_mutex_unlock(&usb_active_accessory_state.mutex);

        return accessory_fd;
    }

    usb_active_accessory_state.accessory_fd = accessory_fd;
    usb_active_accessory_state.is_accessory_connected = TRUE;

    LOG_I(usb_active_accessory_log_tag, "Device configured");

    pthread_mutex_unlock(&usb_active_accessory_state.mutex);

    // Start read HB thread

    pthread_create(
        &read_hb_thread,
        NULL,
        usb_active_accessory_read_hb,
        NULL
    );

    if (usb_active_accessory_probe() == TRUE) {
        
        pthread_create(
            &processing_thread,
            NULL,
            usb_active_accessory_processor,
            NULL
        );

        pthread_create(
            &write_hb_thread,
            NULL,
            usb_active_accessory_write_hb,
            NULL
        );

        LOG_I(usb_active_accessory_log_tag, "Accessory processor started!");
    }

    if (read_hb_thread > 0) {
        pthread_join(read_hb_thread, NULL);
    }

    LOG_I(usb_active_accessory_log_tag, "Accessory disconnected");

    // Cleanup

    pthread_mutex_lock(&usb_active_accessory_state.mutex);

    LOG_I(usb_active_accessory_log_tag, "Deactivating video receiver...");
    video_receiver_deactivate();

    LOG_I(usb_active_accessory_log_tag, "Closing accessory fd...");
    
    usb_accessory_disable();
    close(usb_active_accessory_state.accessory_fd);

    usb_active_accessory_state.accessory_fd = -1;
    usb_active_accessory_state.is_accessory_connected = FALSE;
    usb_active_accessory_state.processor = NULL;

    pthread_mutex_unlock(&usb_active_accessory_state.mutex);

    if (write_hb_thread > 0) {
        LOG_I(usb_active_accessory_log_tag, "Waiting write hb thread");

        pthread_join(write_hb_thread, NULL);
    }

    if (processing_thread > 0) {
        LOG_I(usb_active_accessory_log_tag, "Waiting processing thread");

        pthread_join(processing_thread, NULL);
    }

    LOG_I(usb_active_accessory_log_tag, "Processing celeanup complete!");

    return 0;
}

int usb_active_accessory_fd(void) {
    int accessory_fd = -1;
    gboolean is_accessory_connected = FALSE;

    pthread_mutex_lock(&usb_active_accessory_state.mutex);

    accessory_fd = usb_active_accessory_state.accessory_fd;
    is_accessory_connected = usb_active_accessory_state.is_accessory_connected;

    pthread_mutex_unlock(&usb_active_accessory_state.mutex);

    if (is_accessory_connected) {
        return accessory_fd;
    }

    return -1;
}

int usb_active_accessory_read(void *buffer, size_t len) {
    int ret = -EIO;
    int accessory_fd = usb_active_accessory_fd();

    if (accessory_fd <= 0) {
        return ret;
    }

    pthread_mutex_lock(&usb_active_accessory_state.read_mutex);

    ret = read(accessory_fd, buffer, len);
    usb_active_accessory_state.last_read_ts = usb_active_accessory_get_ts();

    pthread_mutex_unlock(&usb_active_accessory_state.read_mutex);

    return ret;
}

int usb_active_accessory_write(void *buffer, size_t len) {
    int ret = -EIO;
    int accessory_fd = usb_active_accessory_fd();

    if (accessory_fd <= 0) {
        return ret;
    }

    pthread_mutex_lock(&usb_active_accessory_state.write_mutex);

    ret = write(accessory_fd, buffer, len);
    usb_active_accessory_state.last_write_ts = usb_active_accessory_get_ts();

    pthread_mutex_unlock(&usb_active_accessory_state.write_mutex);

    return ret;
}

int usb_active_accessory_write_with_padding_512(
    void *header,
    size_t header_len,
    void *body,
    size_t body_len
) {
    static uint8_t *send_buffer = NULL;
    static size_t send_buffer_size = 0;

    int written = 0;
    int accessory_fd = usb_active_accessory_fd();
    uint8_t *send_buffer_cursor = NULL;
    size_t padding_size = 0;
    size_t full_message_size = header_len + body_len;
    size_t chunked_message_size = (full_message_size >> USB_ACTIVE_ACCESSORY_CHUNK_SIZE_512_LOG2) << USB_ACTIVE_ACCESSORY_CHUNK_SIZE_512_LOG2;

    if (chunked_message_size < full_message_size) {
        chunked_message_size += USB_ACTIVE_ACCESSORY_CHUNK_SIZE_512;
    }

    padding_size = chunked_message_size - full_message_size;

    if (accessory_fd <= 0) {
        return -EIO;
    }

    pthread_mutex_lock(&usb_active_accessory_state.write_mutex);

    if ((chunked_message_size > send_buffer_size) || (send_buffer == NULL)) {
        uint8_t *resized_buffer = (uint8_t*)realloc(send_buffer, chunked_message_size);

        if (resized_buffer == NULL) {
            pthread_mutex_unlock(&usb_active_accessory_state.write_mutex);

            return -ENOMEM;
        }

        send_buffer = resized_buffer;
        send_buffer_size = chunked_message_size;
    }

    send_buffer_cursor = send_buffer;

    if (header && header_len) {
        memcpy(
            send_buffer_cursor,
            header,
            header_len
        );

        send_buffer_cursor += header_len;
    }

    if (body && body_len) {
        memcpy(
            send_buffer_cursor,
            body,
            body_len
        );

        send_buffer_cursor += body_len;
    }

    if (padding_size) {
        memset(
            send_buffer_cursor,
            0,
            padding_size
        );

        send_buffer_cursor += padding_size;
    }

    written = write(accessory_fd, send_buffer, chunked_message_size);
    
    pthread_mutex_unlock(&usb_active_accessory_state.write_mutex);

    return written;
}

int usb_active_accessory_write_h264(message_processor_video_params_t video_parameters, void *buffer, size_t len) {
    int result = -EINVAL;

    if (usb_active_accessory_state.processor == NULL) {
        LOG_E(usb_active_accessory_log_tag, "Can`t write video frame. Processor is NULL!");

        return result;
    }

    result = usb_active_accessory_state.processor->write_h264(
        video_parameters,
        buffer,
        len
    );

    return result;
}

#pragma mark - Private function implementations

unsigned long usb_active_accessory_get_ts(void) {
	struct timeval tv;
	
	gettimeofday(&tv, NULL);
	
	return USB_ACTIVE_ACCESSORY_WORKER_SEC_TO_US(tv.tv_sec) + tv.tv_usec;
}

void* usb_active_accessory_read_hb(void *arg) {

    pthread_mutex_lock(&usb_active_accessory_state.read_mutex);

    usb_active_accessory_state.last_read_ts = usb_active_accessory_get_ts();

    pthread_mutex_unlock(&usb_active_accessory_state.read_mutex);

    while (usb_active_accessory_state.is_accessory_connected && video_receiver_is_connected())
    {
        unsigned long elapsed_time = usb_active_accessory_get_ts() - usb_active_accessory_state.last_read_ts;

        if (elapsed_time >= USB_ACTIVE_ACCESSORY_WORKER_SEC_TO_US(USB_ACTIVE_ACCESSORY_READ_HB_INTERVAL_SEC)) {
            break;
        }

        usleep(USB_ACTIVE_ACCESSORY_HB_CHECK_PERIOD_US);
    }

    return NULL;
}

void* usb_active_accessory_write_hb(void *arg) {

    while (usb_active_accessory_state.is_accessory_connected)
    {
        unsigned long elapsed_time = usb_active_accessory_get_ts() - usb_active_accessory_state.last_write_ts;

        if (elapsed_time >= USB_ACTIVE_ACCESSORY_WORKER_SEC_TO_US(USB_ACTIVE_ACCESSORY_WRITE_HB_INTERVAL_SEC)) {
            usb_active_accessory_send_heartbeat();
        }

        usleep(USB_ACTIVE_ACCESSORY_HB_CHECK_PERIOD_US);
    }

    return NULL;
}

void* usb_active_accessory_processor(void *arg) {

    while (usb_active_accessory_state.is_accessory_connected)
    {

        if (usb_active_accessory_state.processor == NULL) {
            LOG_E(usb_active_accessory_log_tag, "usb_active_accessory_processor thread is started but, processor is NULL!");

            return NULL;
        }

        if (usb_active_accessory_state.processor->run() == FALSE) {
            LOG_E(usb_active_accessory_log_tag, "Message processing error!");
        }
    }

    return NULL;
}

void usb_active_accessory_send_heartbeat(void) {

    if (usb_active_accessory_state.processor == NULL) {
        LOG_E(usb_active_accessory_log_tag, "Can`t send heartbeat. Processor is NULL!");

        return;
    }

    usb_active_accessory_state.processor->write_hb();
}

gboolean usb_active_accessory_probe(void) {
    uint8_t* buffer = (uint8_t*)malloc(MESSAGE_DEFAULT_CHUNK_SIZE);

    const message_processor_t* processors[] = {
        message_processor_v1_get(),
        message_processor_v2_get()
    };

    if (!buffer) {
        LOG_E(usb_active_accessory_log_tag, "Can`t allocate memory for probing!");

        return FALSE;
    }
    
    memset((void*)buffer, 0, MESSAGE_DEFAULT_CHUNK_SIZE);
    
    // Send an app_status request to check the protocol type

    app_status_response_init((app_status_t *)buffer);

    while(usb_active_accessory_write((void*)buffer, MESSAGE_DEFAULT_CHUNK_SIZE) < 0) {

        if (errno != ENODEV) {
            break;
        }

        LOG_I(usb_active_accessory_log_tag, "Awaiting accessory ...");
        
        usleep(USB_ACTIVE_ACCESSORY_PROBE_PRERIOD_US);
    }

    LOG_I(usb_active_accessory_log_tag, "Reading response ...");

    if (usb_active_accessory_read((void*)buffer, MESSAGE_DEFAULT_CHUNK_SIZE) <= 0) {
        free(buffer);
    
        return FALSE;
    }

    pthread_mutex_lock(&usb_active_accessory_state.processor_mutex);

    LOG_I(usb_active_accessory_log_tag, "Searching for processor (total count: %d)", sizeof(processors) / sizeof(message_processor_t*));

    for (int i = 0; i < sizeof(processors) / sizeof(message_processor_t*); i++) {
        const message_processor_t* current_processor = processors[i];

        if (current_processor->probe(buffer, MESSAGE_DEFAULT_CHUNK_SIZE) == TRUE) {
            usb_active_accessory_state.processor = current_processor;
            
            break;
        }
    }

    LOG_I(usb_active_accessory_log_tag, "Probe finished: %s", usb_active_accessory_state.processor == NULL ? "NOT FOUND" : "OK");

    if (usb_active_accessory_state.processor == NULL) {
        pthread_mutex_unlock(&usb_active_accessory_state.processor_mutex);
        free(buffer);
        
        return FALSE;
    }

    pthread_mutex_unlock(&usb_active_accessory_state.processor_mutex);
    free(buffer);

    return TRUE;
}
