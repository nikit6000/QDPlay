#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include "accessory.h"
#include "usb_accessory.h"
#include "usb_accessory_worker.h"
#include "usb_accessory_message_processor.h"

#pragma mark - Private definitions

#define USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(x)  do { if (x != 0) return x;  } while(0);
#define USB_ACCESSORY_WORKER_USB_STATUS_IO          "/sys/class/udc/musb-hdrc.5.auto/device/gadget.0/suspended"
#define USB_ACCESSORY_WORKER_USB_DRIVER_IO          "/dev/usb_accessory"
#define USB_ACCESSORY_WORKER_POLL_INTERVAL_US       (10000)
#define USB_ACCESSORY_WORKER_USB_POLL_INTERVAL_US   (500000)
#define USB_ACCESSORY_WORKER_PACKET_SIZE            (512)
#define USB_ACCESSORY_WORKER_EPOLL_MAX_EVENTS       (1)
#define USB_ACCESSORY_WORKER_POLL_TIMEOUT_MS        (500)

#pragma mark - Private methods definition

int usb_accessory_worker_is_usb_suspended(void);
int usb_accessory_worker_is_start_requested(void);
void * usb_accessory_worker_initial_thread(void* arg);
void * usb_accessory_worker_configured_thread(void* arg);
void * usb_accessory_worker_configured_watcher_thread(void* arg);

#pragma mark - Internal methods implementation

#pragma mark - Private methods implementation

int usb_accessory_worker_is_usb_suspended(void) {
    int fd = open(USB_ACCESSORY_WORKER_USB_STATUS_IO, O_RDONLY);
    
    char value = '0';

    if(read(fd, &value, 1)) {
        return (int)(value - '0');
    }

    printf("Can`t reader suspended status!\n");

    return 0;
}

int usb_accessory_worker_is_start_requested(void) {
    int fd = open(USB_ACCESSORY_WORKER_USB_DRIVER_IO, O_RDWR);

    if (fd < 0) {
        printf("Could not open %s\n", USB_ACCESSORY_WORKER_USB_DRIVER_IO);
        return 0;
    }

    int result = ioctl(fd, ACCESSORY_IS_START_REQUESTED);

    close(fd);
    
    return result;
}

int usb_accessory_worker_start(void) {

    while (1){
        pthread_t initial_device_thread_id;
        pthread_t configured_device_thread_id;
        int thread_result = 0;

        int result = pthread_create(
            &initial_device_thread_id,
            NULL,
            usb_accessory_worker_initial_thread,
            NULL
        );
        USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);

        result = pthread_join(initial_device_thread_id, NULL);
        USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);
        USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(thread_result);

        result = pthread_create(
            &configured_device_thread_id,
            NULL,
            usb_accessory_worker_configured_thread,
            NULL
        );
        USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);

        result = pthread_join(configured_device_thread_id, NULL);
        USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);
        USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(thread_result);
    }
}

void * usb_accessory_worker_initial_thread(void* arg) {

    printf("Starting initial device ...\n");

    int result = usb_accessory_create(usb_accessory_initial);

    while (usb_accessory_worker_is_start_requested() == 0) {
        usleep(USB_ACCESSORY_WORKER_POLL_INTERVAL_US);
    }

    pthread_exit(&result);
}

void * usb_accessory_worker_configured_thread(void* arg) {

    printf("Starting configured device ...\n");

    pthread_t usb_watcher_thread_id;
    int result = usb_accessory_create(usb_accessory_configured);
    int accessory_fd = open(USB_ACCESSORY_WORKER_USB_DRIVER_IO, O_RDWR);

    if (accessory_fd < 0) {
        printf("Failed to open accessory file descriptor\n");

        goto out;
    }

    if (usb_accessory_message_processor_setup(accessory_fd) != USB_ACCESSORY_MSG_OK) {
        goto flush;
    }

    result = pthread_create(
        &usb_watcher_thread_id,
        NULL,
        usb_accessory_worker_configured_watcher_thread,
        NULL
    );

    if (result != 0) {
        goto flush;
    }

    uint8_t buffer[USB_ACCESSORY_WORKER_PACKET_SIZE] = { 0 };
    
    printf("Configured device started ...\n");

    while (1) {
        usb_accessory_msg_processor_status_t status;

        status = usb_accessory_message_processor_handle(accessory_fd);

        if (status != USB_ACCESSORY_MSG_OK) {
            break;
        }
    }

    pthread_join(usb_watcher_thread_id, NULL);

flush:

    if (accessory_fd >= 0) 
        close(accessory_fd);

out:
    pthread_exit(&result);
}

void * usb_accessory_worker_configured_watcher_thread(void* arg) {

    // Wait until disconnect event

    printf("USB watcher thread started\n");

    while (usb_accessory_worker_is_usb_suspended() == 0) {
       usleep(USB_ACCESSORY_WORKER_USB_POLL_INTERVAL_US);
    }

    printf("USB disconnected\n");

    usb_accessory_remove_all();
}