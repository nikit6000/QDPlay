#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <glib.h>
#include "accessory.h"
#include "services/video_receiver/video_receiver.h"
#include "usb_accessory/usb_accessory.h"
#include "usb_accessory/usb_accessory_worker.h"
#include "usb_accessory/usb_active_accessory.h"
#include "logging/logging.h"

/*
 * Android Open Accessory emulation, device side.
 *
 *   1. INITIAL: the gadget looks like a Samsung phone. The head unit (USB host) sends
 *      ACCESSORY_GET_PROTOCOL / SEND_STRING / START; f_accessory answers them and
 *      raises start_requested (plus an "ACCESSORY=START" uevent).
 *   2. ACCESSORY: the gadget re-enumerates as 18d1:2d00, the head unit opens the bulk
 *      endpoints and the QDLink session runs (usb_active_accessory_run_session).
 *   3. When the session ends, everything starts over from INITIAL.
 *
 * The gadget is only presented while at least one video source is connected.
 */

#pragma mark - Private definitions

#define USB_ACCESSORY_WORKER_DEVICE_PATH            "/dev/usb_accessory"
// Fallback polling of start_requested if uevents are not available
#define USB_ACCESSORY_WORKER_POLL_INTERVAL_MS       (100)
#define USB_ACCESSORY_WORKER_RETRY_MIN_US           (250000)
#define USB_ACCESSORY_WORKER_RETRY_MAX_US           (5000000)
#define USB_ACCESSORY_WORKER_UEVENT_BUFFER_SIZE     (4096)

typedef enum {
    WAIT_START_REQUESTED = 0,
    WAIT_NO_SOURCE,
    WAIT_ERROR
} wait_result_t;

#pragma mark - Private properties

static const gchar* usb_accessory_worker_log_tag = "AOA";

#pragma mark - Helpers

static void worker_sleep_us(long us) {
    struct timespec request = { .tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000 };
    struct timespec remaining;

    while (nanosleep(&request, &remaining) < 0 && errno == EINTR) {
        request = remaining;
    }
}

static int open_uevent_socket(void) {
    struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_pid = 0, .nl_groups = 1 };
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);

    if (fd < 0) {
        return -1;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);

        return -1;
    }

    return fd;
}

static void drain_uevents(int uevent_fd) {
    char buffer[USB_ACCESSORY_WORKER_UEVENT_BUFFER_SIZE];

    while (uevent_fd >= 0 && recv(uevent_fd, buffer, sizeof(buffer), MSG_DONTWAIT) > 0) {
        // Only used as a wakeup, the state is read with ioctl()
    }
}

static int open_accessory_device(void) {
    for (int attempt = 0; attempt < 30; attempt++) {
        int fd = open(USB_ACCESSORY_WORKER_DEVICE_PATH, O_RDWR | O_CLOEXEC);

        if (fd >= 0) {
            return fd;
        }

        if (errno != EBUSY && errno != ENOENT && errno != ENODEV) {
            break;
        }

        worker_sleep_us(100000);
    }

    LOG_E(usb_accessory_worker_log_tag, "Can`t open %s: %s", USB_ACCESSORY_WORKER_DEVICE_PATH, strerror(errno));

    return -1;
}

static void log_accessory_strings(int fd) {
    // Strings sent by the head unit with ACCESSORY_SEND_STRING: the best hint which
    // head unit software we talk to
    char manufacturer[256] = "", model[256] = "", description[256] = "", version[256] = "";

    ioctl(fd, ACCESSORY_GET_STRING_MANUFACTURER, manufacturer);
    ioctl(fd, ACCESSORY_GET_STRING_MODEL, model);
    ioctl(fd, ACCESSORY_GET_STRING_DESCRIPTION, description);
    ioctl(fd, ACCESSORY_GET_STRING_VERSION, version);

    LOG_I(usb_accessory_worker_log_tag, "Head unit: manufacturer=\"%s\" model=\"%s\" description=\"%s\" version=\"%s\" audio=%d",
          manufacturer, model, description, version, ioctl(fd, ACCESSORY_GET_AUDIO_MODE));
}

static wait_result_t wait_for_start(int fd, int uevent_fd) {
    while (1) {
        if (!video_receiver_is_connected()) {
            return WAIT_NO_SOURCE;
        }

        int status = ioctl(fd, ACCESSORY_IS_START_REQUESTED);

        if (status < 0) {
            LOG_E(usb_accessory_worker_log_tag, "ACCESSORY_IS_START_REQUESTED failed: %s", strerror(errno));

            return WAIT_ERROR;
        }

        if (status == 1) {
            return WAIT_START_REQUESTED;
        }

        // Sleep until a uevent from f_accessory (GETPROTOCOL/SENDSTRING/START) or the poll interval
        struct pollfd pfd = { .fd = uevent_fd, .events = POLLIN };

        if (uevent_fd >= 0) {
            if (poll(&pfd, 1, USB_ACCESSORY_WORKER_POLL_INTERVAL_MS) > 0) {
                drain_uevents(uevent_fd);
            }
        } else {
            worker_sleep_us(USB_ACCESSORY_WORKER_POLL_INTERVAL_MS * 1000);
        }
    }
}

#pragma mark - Internal methods implementation

int usb_accessory_worker_start(void) {
    long retry_delay = USB_ACCESSORY_WORKER_RETRY_MIN_US;
    int uevent_fd = open_uevent_socket();

    usb_active_accessory_install_signal_handler();

    if (uevent_fd < 0) {
        LOG_W(usb_accessory_worker_log_tag, "No uevent socket, polling the accessory state");
    }

    while (1) {
        gboolean failed = FALSE;

        video_receiver_await_connection();

        if (usb_accessory_set_mode(USB_ACCESSORY_MODE_INITIAL) < 0) {
            failed = TRUE;
        } else {
            int fd = open_accessory_device();

            if (fd < 0) {
                failed = TRUE;
            } else {
                drain_uevents(uevent_fd);

                LOG_I(usb_accessory_worker_log_tag, "Waiting for the head unit to start accessory mode ...");

                wait_result_t result = wait_for_start(fd, uevent_fd);

                if (result == WAIT_START_REQUESTED) {
                    log_accessory_strings(fd);
                }

                close(fd);

                if (result == WAIT_START_REQUESTED) {
                    int session_result = usb_active_accessory_run_session();

                    failed = session_result < 0;

                    if (session_result >= 0) {
                        // A completed session is not a failure: start over quickly
                        retry_delay = USB_ACCESSORY_WORKER_RETRY_MIN_US;
                    }
                } else {
                    failed = result == WAIT_ERROR;
                }
            }
        }

        usb_accessory_disable();

        if (failed) {
            // Exponential backoff: a missing UDC or a broken driver must not burn the CPU
            LOG_W(usb_accessory_worker_log_tag, "Retrying in %ld ms", retry_delay / 1000);

            worker_sleep_us(retry_delay);

            retry_delay *= 2;

            if (retry_delay > USB_ACCESSORY_WORKER_RETRY_MAX_US) {
                retry_delay = USB_ACCESSORY_WORKER_RETRY_MAX_US;
            }
        }
    }

    return 0;
}
