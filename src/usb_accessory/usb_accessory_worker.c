#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "accessory.h"
#include "usb_accessory/usb_accessory.h"
#include "usb_accessory/usb_accessory_worker.h"
#include "usb_accessory/usb_accessory_message_processor.h"
#include "usb_accessory/usb_active_accessory.h"
#include "logging/logging.h"

#pragma mark - Private definitions

#define USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(x) \
	do                                             \
	{                                              \
		if (x != 0)                                \
			return x;                              \
	} while (0);

#define USB_ACCESSORY_WORKER_USB_DRIVER_IO 						"/dev/usb_accessory"
#define USB_ACCESSORY_WORKER_POLL_INTERVAL_US 					(10000)

#pragma mark - Private properties

const gchar* usb_accessory_worker_log_tag = "Inactive accessory";
#pragma mark - Private methods definition

int usb_accessory_worker_is_start_requested(void);
void *usb_accessory_worker_initial_thread(void *arg);

#pragma mark - Internal methods implementation

int usb_accessory_worker_start(void)
{
	while (1) {
		pthread_t initial_device_thread_id;
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

		result = usb_active_accessory_create_and_wait(
			USB_ACCESSORY_WORKER_USB_DRIVER_IO
		);

		USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);
	}

	return 0;
}

#pragma mark - Private methods implementation

int usb_accessory_worker_is_start_requested(void)
{
	int fd = open(USB_ACCESSORY_WORKER_USB_DRIVER_IO, O_RDWR);

	if (fd < 0)
	{
		LOG_E(usb_accessory_worker_log_tag, "Could not open %s", USB_ACCESSORY_WORKER_USB_DRIVER_IO);

		return 0;
	}

	int result = ioctl(fd, ACCESSORY_IS_START_REQUESTED);

	close(fd);

	return result;
}

void *usb_accessory_worker_initial_thread(void *arg)
{

	LOG_I(usb_accessory_worker_log_tag, "Starting initial device ...");

	int result = usb_accessory_reset();

	if (result < 0) {
		LOG_E(usb_accessory_worker_log_tag, "Can`t reset USB accessory!");

		exit(EXIT_FAILURE);
	}

	while (usb_accessory_worker_is_start_requested() == 0)
	{
		usleep(USB_ACCESSORY_WORKER_POLL_INTERVAL_US);
	}

	pthread_exit(&result);

	return NULL;
}
