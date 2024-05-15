#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <glib.h>
#include "accessory.h"
#include "services/video_receiver/video_receiver.h"
#include "usb_accessory/usb_accessory.h"
#include "usb_accessory/usb_accessory_worker.h"
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
		gboolean initial_device_result = FALSE;

		// Await for video source connection

		video_receiver_await_connection();

		// When video source is connected 

		int result = pthread_create(
			&initial_device_thread_id,
			NULL,
			usb_accessory_worker_initial_thread,
			&initial_device_result
		);

		USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);

		result = pthread_join(initial_device_thread_id, NULL);

		USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);

		if (initial_device_result == FALSE) {
			usb_accessory_disable();

			continue;
		}

		result = usb_active_accessory_create_and_wait(
			USB_ACCESSORY_WORKER_USB_DRIVER_IO
		);

		USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(result);
	}

	return 0;
}

#pragma mark - Private methods implementation

void *usb_accessory_worker_initial_thread(void *arg)
{
	if (arg == NULL) {
		return NULL;
	}

	gboolean *result = (gboolean*)arg;
	
	LOG_I(usb_accessory_worker_log_tag, "Starting initial device ...");

	if (usb_accessory_reset() < 0) {
		LOG_E(usb_accessory_worker_log_tag, "Can`t reset USB accessory!");

		exit(EXIT_FAILURE);
	}

	int usb_accessory = open(USB_ACCESSORY_WORKER_USB_DRIVER_IO, O_RDWR);

	if (usb_accessory < 0) {
		*result = FALSE;

		pthread_exit(NULL);

		return NULL;
	}

	while (1)
	{
		int status = ioctl(usb_accessory, ACCESSORY_IS_START_REQUESTED);

		if ((status < 0) || (video_receiver_is_connected() == FALSE)) {
			*result = FALSE;

			break;
		} else if (status == 1) {
			*result = TRUE;

			break;
		}

		usleep(USB_ACCESSORY_WORKER_POLL_INTERVAL_US);
	}

	close(usb_accessory);

	pthread_exit(NULL);

	return NULL;
}
