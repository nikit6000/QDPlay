#include <pthread.h>
#include <glib.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "messages/v1/app_status.h"
#include "usb_accessory/message_processor/v1/message_processor_v1.h"
#include "usb_accessory/message_processor/v2/message_processor_v2.h"
#include "usb_accessory/usb_active_accessory.h"
#include "usb_accessory/usb_accessory.h"
#include "services/video_receiver/video_receiver.h"
#include "logging/logging.h"

/*
 * One head unit session in accessory mode.
 *
 * Threads:
 *   - session thread (the caller of usb_active_accessory_run_session): the only reader,
 *     probes the protocol and runs the message processor;
 *   - watchdog thread: heartbeats and timeouts. To stop the session it sets `stopping`,
 *     detaches the gadget (wakes blocked writers and reads that wait for data) and keeps
 *     signalling the session thread: f_accessory waits for the "online" state with
 *     wait_event_interruptible(), and only a signal gets a reader out of there;
 *   - writers from other threads (video receiver) only take the write lock.
 */

#pragma mark - Private definitions

#define USB_ACTIVE_ACCESSORY_DEVICE_PATH            "/dev/usb_accessory"
#define USB_ACTIVE_ACCESSORY_CHUNK_SIZE             (512)
// f_accessory never transfers more than this per read()
#define USB_ACTIVE_ACCESSORY_MAX_READ_CHUNK         (16384)
#define USB_ACTIVE_ACCESSORY_WRITE_HB_INTERVAL_US   (3 * G_USEC_PER_SEC)
// Re-enumeration as accessory + protocol probe
#define USB_ACTIVE_ACCESSORY_CONNECT_TIMEOUT_US     (10 * G_USEC_PER_SEC)
#define USB_ACTIVE_ACCESSORY_WATCHDOG_PERIOD_US     (50000)
#define USB_ACTIVE_ACCESSORY_OPEN_RETRY_US          (100000)
#define USB_ACTIVE_ACCESSORY_OPEN_ATTEMPTS          (30)
// That many unparsable messages in a row mean the stream is out of sync
#define USB_ACTIVE_ACCESSORY_MAX_PROTOCOL_ERRORS    (20)
// Extra messages to look at if the first one does not identify the protocol
// (f_accessory may hand out a stale buffer of the previous session first)
#define USB_ACTIVE_ACCESSORY_PROBE_EXTRA_READS      (2)

typedef enum {
    SESSION_IDLE = 0,
    SESSION_CONNECTING,
    SESSION_RUNNING
} session_phase_t;

#pragma mark - Private types

typedef struct {
    pthread_mutex_t mutex;          // fd, phase, processor
    pthread_mutex_t write_mutex;    // serializes writers, protects send_buffer

    int fd;
    session_phase_t phase;
    const message_processor_t* volatile processor;

    pthread_t session_thread;
    volatile gboolean stopping;
    volatile gboolean link_failed;
    volatile gboolean session_done;

    volatile gint64 last_read_ts;
    volatile gint64 last_write_ts;
    gint64 started_ts;

    // Lossless reads: the driver rounds a read up to the packet size and drops what does not
    // fit into the user buffer, so always read whole packets and keep the rest here
    uint8_t bounce[USB_ACTIVE_ACCESSORY_MAX_READ_CHUNK];
    size_t bounce_len;
    size_t bounce_pos;

    uint8_t *send_buffer;
    size_t send_buffer_size;
} session_t;

static const gchar* usb_active_accessory_log_tag = "Accessory";

#pragma mark - Private properties

static session_t session = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .write_mutex = PTHREAD_MUTEX_INITIALIZER,
    .fd = -1,
    .phase = SESSION_IDLE,
    .processor = NULL
};

#pragma mark - Helpers

static void wakeup_signal_handler(int signo) {
    (void)signo;
}

void usb_active_accessory_install_signal_handler(void) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = wakeup_signal_handler;
    sigemptyset(&action.sa_mask);
    // No SA_RESTART: blocking syscalls must return EINTR
    action.sa_flags = 0;

    sigaction(USB_ACTIVE_ACCESSORY_WAKEUP_SIGNAL, &action, NULL);
}

static gboolean session_stopping(void) {
    return __atomic_load_n(&session.stopping, __ATOMIC_ACQUIRE);
}

static void session_request_stop(const char *reason) {
    if (!__atomic_exchange_n(&session.stopping, TRUE, __ATOMIC_ACQ_REL)) {
        LOG_I(usb_active_accessory_log_tag, "Stopping session: %s", reason);
    }
}

static int session_fd(void) {
    int fd;

    pthread_mutex_lock(&session.mutex);
    fd = session.fd;
    pthread_mutex_unlock(&session.mutex);

    return fd;
}

static gboolean is_link_error(int err) {
    // f_accessory returns ENODEV from write() until the head unit configures the accessory
    // (the "online" state). That is expected while connecting, only a running session is lost.
    if (err == ENODEV) {
        return session.processor != NULL;
    }

    return err == EIO || err == ESHUTDOWN || err == EPIPE || err == ECONNRESET;
}

#pragma mark - I/O

int usb_active_accessory_read(void *buffer, size_t len) {
    int fd = session_fd();

    if (fd < 0 || session_stopping()) {
        errno = ECANCELED;
        return -1;
    }

    if (len == 0) {
        return 0;
    }

    // Leftover of the previous transfer first
    if (session.bounce_pos >= session.bounce_len) {
        size_t request = (len + USB_ACTIVE_ACCESSORY_CHUNK_SIZE - 1) & ~((size_t)USB_ACTIVE_ACCESSORY_CHUNK_SIZE - 1);
        ssize_t readed;

        if (request > sizeof(session.bounce)) {
            request = sizeof(session.bounce);
        }

        do {
            readed = read(fd, session.bounce, request);
        } while (readed < 0 && errno == EINTR && !session_stopping());

        if (readed < 0) {
            int err = errno;

            if (err != EINTR && is_link_error(err)) {
                session.link_failed = TRUE;
            }

            errno = err;

            return -1;
        }

        session.bounce_len = (size_t)readed;
        session.bounce_pos = 0;

        if (readed > 0) {
            session.last_read_ts = g_get_monotonic_time();
        }
    }

    size_t available = session.bounce_len - session.bounce_pos;
    size_t copied = available < len ? available : len;

    memcpy(buffer, session.bounce + session.bounce_pos, copied);
    session.bounce_pos += copied;

    return (int)copied;
}

int usb_active_accessory_read_message_tail(uint8_t *buffer, size_t received, size_t required, size_t capacity) {
    // A message is one USB transfer padded to 512 bytes, but a transfer may arrive in
    // several reads (and some head units do not pad), so read until the declared size is here

    if (buffer == NULL || required > capacity) {
        return -EINVAL;
    }

    while (received < required) {
        int readed = usb_active_accessory_read(buffer + received, capacity - received);

        if (readed <= 0) {
            return readed < 0 ? -errno : -EIO;
        }

        received += (size_t)readed;
    }

    return (int)received;
}

// Caller holds write_mutex
static int write_all_locked(int fd, const uint8_t *buffer, size_t len) {
    size_t written = 0;

    while (written < len) {
        ssize_t ret = write(fd, buffer + written, len - written);

        if (ret < 0 && errno == EINTR && !session_stopping()) {
            continue;
        }

        if (ret <= 0) {
            int err = ret < 0 ? errno : EIO;

            if (is_link_error(err)) {
                session.link_failed = TRUE;
            }

            return -err;
        }

        written += (size_t)ret;
    }

    session.last_write_ts = g_get_monotonic_time();

    return (int)written;
}

int usb_active_accessory_write(void *buffer, size_t len) {
    int ret = -ENOTCONN;

    pthread_mutex_lock(&session.write_mutex);

    int fd = session_fd();

    if (fd >= 0 && !session_stopping()) {
        ret = write_all_locked(fd, (const uint8_t*)buffer, len);
    }

    pthread_mutex_unlock(&session.write_mutex);

    if (ret < 0) {
        errno = -ret;
    }

    return ret;
}

int usb_active_accessory_write_with_padding_512(void *header, size_t header_len, void *body, size_t body_len) {
    size_t full_message_size = header_len + body_len;
    size_t chunked_message_size = (full_message_size + USB_ACTIVE_ACCESSORY_CHUNK_SIZE - 1) & ~((size_t)USB_ACTIVE_ACCESSORY_CHUNK_SIZE - 1);
    int ret = -ENOTCONN;

    pthread_mutex_lock(&session.write_mutex);

    int fd = session_fd();

    if (fd < 0 || session_stopping()) {
        pthread_mutex_unlock(&session.write_mutex);

        return -ENOTCONN;
    }

    if (chunked_message_size > session.send_buffer_size) {
        uint8_t *resized_buffer = (uint8_t*)realloc(session.send_buffer, chunked_message_size);

        if (resized_buffer == NULL) {
            pthread_mutex_unlock(&session.write_mutex);

            return -ENOMEM;
        }

        session.send_buffer = resized_buffer;
        session.send_buffer_size = chunked_message_size;
    }

    uint8_t *cursor = session.send_buffer;

    if (header && header_len) {
        memcpy(cursor, header, header_len);
        cursor += header_len;
    }

    if (body && body_len) {
        memcpy(cursor, body, body_len);
        cursor += body_len;
    }

    memset(cursor, 0, chunked_message_size - full_message_size);

    ret = write_all_locked(fd, session.send_buffer, chunked_message_size);

    pthread_mutex_unlock(&session.write_mutex);

    return ret;
}

int usb_active_accessory_write_h264(message_processor_video_params_t video_parameters, void *buffer, size_t len) {
    // Frames arrive from the video receiver at any time; only a probed session takes them
    const message_processor_t* processor = session.processor;

    if (processor == NULL || session_stopping()) {
        return -ENOTCONN;
    }

    return processor->write_h264(video_parameters, buffer, len);
}

#pragma mark - Watchdog

static void *watchdog_thread(void *arg) {
    (void)arg;

    while (!__atomic_load_n(&session.session_done, __ATOMIC_ACQUIRE)) {
        gint64 now = g_get_monotonic_time();
        const message_processor_t* processor = session.processor;

        if (!session_stopping()) {
            if (!video_receiver_is_connected()) {
                session_request_stop("no video sources");
            } else if (session.link_failed) {
                session_request_stop("USB link error");
            } else if (processor == NULL) {
                if (now - session.started_ts > USB_ACTIVE_ACCESSORY_CONNECT_TIMEOUT_US) {
                    session_request_stop("head unit did not connect");
                }
            } else {
                gint64 timeout = (gint64)processor->read_timeout_sec * G_USEC_PER_SEC;

                if (now - session.last_read_ts > timeout) {
                    session_request_stop("head unit is silent");
                } else if (now - session.last_write_ts >= USB_ACTIVE_ACCESSORY_WRITE_HB_INTERVAL_US) {
                    processor->write_hb();
                }
            }

            if (session_stopping()) {
                // Detaching completes queued USB requests: blocked writers and readers
                // waiting for data return with an error
                usb_accessory_disable();
            }
        }

        if (session_stopping()) {
            // A reader may wait for the "online" state, only a signal gets it out. Repeat it:
            // the session thread may be between the stop check and the read() call.
            pthread_kill(session.session_thread, USB_ACTIVE_ACCESSORY_WAKEUP_SIGNAL);
        }

        usleep(USB_ACTIVE_ACCESSORY_WATCHDOG_PERIOD_US);
    }

    return NULL;
}

#pragma mark - Session

static const char *device_path(void) {
    // Test hook: tests substitute the driver with a pseudo terminal
    const char *path = getenv("QDPLAY_ACCESSORY_DEVICE");

    return path ? path : USB_ACTIVE_ACCESSORY_DEVICE_PATH;
}

static int open_device(void) {
    for (int attempt = 0; attempt < USB_ACTIVE_ACCESSORY_OPEN_ATTEMPTS; attempt++) {
        int fd = open(device_path(), O_RDWR | O_CLOEXEC | O_NOCTTY);

        if (fd >= 0) {
            return fd;
        }

        // EBUSY: the previous descriptor is being released; ENOENT: module still loading
        if (errno != EBUSY && errno != ENOENT && errno != ENODEV) {
            LOG_E(usb_active_accessory_log_tag, "Can`t open %s: %s", device_path(), strerror(errno));

            return -errno;
        }

        usleep(USB_ACTIVE_ACCESSORY_OPEN_RETRY_US);
    }

    LOG_E(usb_active_accessory_log_tag, "%s stays busy", device_path());

    return -EBUSY;
}

static const message_processor_t* probe_protocol(void) {
    uint8_t buffer[USB_ACTIVE_ACCESSORY_CHUNK_SIZE];
    const message_processor_t* processors[] = {
        message_processor_v1_get(),
        message_processor_v2_get()
    };

    // The head unit expects the phone to report its state first

    memset(buffer, 0, sizeof(buffer));
    app_status_response_init((app_status_t *)buffer);

    if (usb_active_accessory_write(buffer, sizeof(buffer)) < 0) {
        // Writes fail until the head unit configures the accessory, the watchdog
        // limits the waiting time
        while (!session_stopping() && usb_active_accessory_write(buffer, sizeof(buffer)) < 0 && errno == ENODEV) {
            usleep(USB_ACTIVE_ACCESSORY_OPEN_RETRY_US);
        }

        if (session_stopping()) {
            return NULL;
        }
    }

    for (int attempt = 0; attempt <= USB_ACTIVE_ACCESSORY_PROBE_EXTRA_READS && !session_stopping(); attempt++) {
        memset(buffer, 0, sizeof(buffer));

        int readed = usb_active_accessory_read(buffer, sizeof(buffer));

        if (readed <= 0) {
            return NULL;
        }

        for (size_t i = 0; i < G_N_ELEMENTS(processors); i++) {
            if (processors[i]->probe(buffer, sizeof(buffer)) == TRUE) {
                return processors[i];
            }
        }

        LOG_W(usb_active_accessory_log_tag, "Unknown first message (%d bytes, starts with %02x %02x %02x %02x)",
              readed, buffer[0], buffer[1], buffer[2], buffer[3]);
    }

    return NULL;
}

static void session_reset_state(int fd) {
    pthread_mutex_lock(&session.mutex);

    session.fd = fd;
    session.phase = SESSION_CONNECTING;
    session.processor = NULL;
    session.session_thread = pthread_self();
    session.stopping = FALSE;
    session.link_failed = FALSE;
    session.session_done = FALSE;
    session.started_ts = g_get_monotonic_time();
    session.last_read_ts = session.started_ts;
    session.last_write_ts = session.started_ts;
    session.bounce_len = 0;
    session.bounce_pos = 0;

    pthread_mutex_unlock(&session.mutex);
}

int usb_active_accessory_run_session(void) {
    pthread_t watchdog;
    int protocol_errors = 0;
    int ret;

    ret = usb_accessory_set_mode(USB_ACCESSORY_MODE_ACCESSORY);

    if (ret < 0) {
        return ret;
    }

    int fd = open_device();

    if (fd < 0) {
        usb_accessory_disable();

        return fd;
    }

    session_reset_state(fd);

    if (pthread_create(&watchdog, NULL, watchdog_thread, NULL) != 0) {
        LOG_E(usb_active_accessory_log_tag, "Can`t start watchdog");

        pthread_mutex_lock(&session.mutex);
        session.fd = -1;
        session.phase = SESSION_IDLE;
        pthread_mutex_unlock(&session.mutex);

        close(fd);
        usb_accessory_disable();

        return -EAGAIN;
    }

    LOG_I(usb_active_accessory_log_tag, "Waiting for the head unit ...");

    const message_processor_t* processor = probe_protocol();

    if (processor != NULL && !session_stopping()) {
        pthread_mutex_lock(&session.mutex);
        session.processor = processor;
        session.phase = SESSION_RUNNING;
        pthread_mutex_unlock(&session.mutex);

        LOG_I(usb_active_accessory_log_tag, "Session started (%s protocol)", processor == message_processor_v2_get() ? "V2" : "V1");

        while (!session_stopping()) {
            if (processor->run() == TRUE) {
                protocol_errors = 0;
                continue;
            }

            if (session.link_failed || session_stopping()) {
                break;
            }

            if (++protocol_errors >= USB_ACTIVE_ACCESSORY_MAX_PROTOCOL_ERRORS) {
                session_request_stop("too many protocol errors");
            }
        }
    } else if (!session_stopping()) {
        session_request_stop("protocol not recognized");
    }

    // Teardown

    session_request_stop(session.link_failed ? "USB link error" : "session finished");
    video_receiver_deactivate();
    usb_accessory_disable();

    __atomic_store_n(&session.session_done, TRUE, __ATOMIC_RELEASE);
    pthread_join(watchdog, NULL);

    // Writers from other threads hold write_mutex while using the descriptor
    pthread_mutex_lock(&session.write_mutex);
    pthread_mutex_lock(&session.mutex);

    close(session.fd);
    session.fd = -1;
    session.processor = NULL;
    session.phase = SESSION_IDLE;

    pthread_mutex_unlock(&session.mutex);
    pthread_mutex_unlock(&session.write_mutex);

    LOG_I(usb_active_accessory_log_tag, "Session closed");

    return 0;
}
