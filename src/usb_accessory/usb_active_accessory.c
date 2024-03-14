#include <pthread.h>
#include <glib.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "usb_accessory/usb_active_accessory.h"
#include "usb_accessory/usb_accessory_message_processor.h"
#include "usb_accessory/usb_accessory.h"
#include "video/video_receiver.h"
#include "logging/logging.h"

#pragma mark - Private definitions

#define USB_ACTIVE_ACCESSORY_WRITE_HB_INTERVAL_SEC              (3)
#define USB_ACTIVE_ACCESSORY_READ_HB_INTERVAL_SEC               (5)
#define USB_ACTIVE_ACCESSORY_HB_CHECK_PERIOD_US 			    (1000)
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
    .write_mutex = PTHREAD_MUTEX_INITIALIZER
};

#pragma mark - Private function definitions

unsigned long usb_active_accessory_get_ts(void);
void* usb_active_accessory_read_hb(void *arg);
void* usb_active_accessory_write_hb(void *arg);
void* usb_active_accessory_processor(void *arg);

#pragma mark - Internal function implementations

int usb_active_accessory_create_and_wait(const char* device_path) {
    int accessory_fd = -1;
    int ret = -EALREADY;
    pthread_t write_hb_thread = 0, read_hb_thread = 0, processing_thread = 0;

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
    
    pthread_mutex_unlock(&usb_active_accessory_state.mutex);

    LOG_I(usb_active_accessory_log_tag, "Device configured");

    // Start HeartBeats and Processing threads 

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

    pthread_create(
        &read_hb_thread,
        NULL,
        usb_active_accessory_read_hb,
        NULL
    );

    LOG_I(usb_active_accessory_log_tag, "Threads started");

    // Wait only heart beat thread

    pthread_join(read_hb_thread, NULL);

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

    pthread_mutex_unlock(&usb_active_accessory_state.mutex);

    LOG_I(usb_active_accessory_log_tag, "Waiting write hb thread");
    pthread_join(write_hb_thread, NULL);

    LOG_I(usb_active_accessory_log_tag, "Waiting processing thread");
    pthread_join(processing_thread, NULL);

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

    while (usb_active_accessory_state.is_accessory_connected)
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
            //usb_active_accessory_write(NULL, 0); // TODO
            LOG_I(usb_active_accessory_log_tag, "Sending HeartBeat");
            usb_active_accessory_state.last_write_ts = usb_active_accessory_get_ts();
        }

        usleep(USB_ACTIVE_ACCESSORY_HB_CHECK_PERIOD_US);
    }

    return NULL;
}

void* usb_active_accessory_processor(void *arg) {

    usb_accessory_message_processor_setup();

    while (usb_active_accessory_state.is_accessory_connected)
    {
        if (usb_accessory_message_processor_handle() != USB_ACCESSORY_MSG_OK) {
			LOG_E(usb_active_accessory_log_tag, "Message processing error!");
		}
    }

    return NULL;
}