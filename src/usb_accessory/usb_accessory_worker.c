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
#include "video/video_receiver.h"
#include "logging/logging.h"

#pragma mark - Private definitions

#define USB_ACCESSORY_WORKER_USB_ASSUME_SUCCESS(x) \
	do                                             \
	{                                              \
		if (x != 0)                                \
			return x;                              \
	} while (0);

#define USB_ACCESSORY_WORKER_USB_DRIVER_IO 					"/dev/usb_accessory"
#define USB_ACCESSORY_WORKER_POLL_INTERVAL_US 				(10000)
#define USB_ACCESSORY_WORKER_USB_HEARTBEAT_INTERVAL_SEC 	(5)
#define USB_ACCESSORY_WORKER_SEC_TO_US(x)					(1000000 * (x))

typedef struct {
	int accessory_fd;
	pthread_t worker_thread;
	gboolean is_connection_was_lost;
} usb_accessory_worker_heartbeat_context_t;

#pragma mark - Private properties

unsigned long usb_accessory_worker_latest_activity_ts = 0;

#pragma mark - Private methods definition

int usb_accessory_worker_is_start_requested(void);
void *usb_accessory_worker_initial_thread(void *arg);
void *usb_accessory_worker_configured_thread(void *arg);
void *usb_accessory_worker_heartbeat_thread(void *arg);
void usb_accessory_worker_heartbeat(void);
unsigned long usb_accessory_worker_get_ts(void);
const gchar* usb_accessory_worker_log_tag = "USB Worker";

#pragma mark - Internal methods implementation

int usb_accessory_worker_start(void)
{
	while (1) {
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

void *usb_accessory_worker_configured_thread(void *arg)
{
	pthread_t heartbeat_thread;
	usb_accessory_worker_heartbeat_context_t heartbeat_context;

	int accessory_fd = -1;

	LOG_I(usb_accessory_worker_log_tag ,"Starting configured device ...");

	if (usb_accessory_configure() < 0) {
		LOG_E(usb_accessory_worker_log_tag, "Can`t configure USB accessory");

		return NULL;
	}

	accessory_fd = open(USB_ACCESSORY_WORKER_USB_DRIVER_IO, O_RDWR);

	if (accessory_fd < 0)
	{
		LOG_E(usb_accessory_worker_log_tag, "Failed to open accessory accessory file descriptor");

		goto out;
	}

	if (usb_accessory_message_processor_setup(accessory_fd) != USB_ACCESSORY_MSG_OK)
	{
		LOG_E(usb_accessory_worker_log_tag, "Can`t setup message processor");

		goto out;
	}

	heartbeat_context.accessory_fd = accessory_fd;
	heartbeat_context.worker_thread = pthread_self();
	heartbeat_context.is_connection_was_lost = FALSE;

	usb_accessory_worker_heartbeat();

	if (
		pthread_create(
			&heartbeat_thread,
			NULL,
			usb_accessory_worker_heartbeat_thread,
			&heartbeat_context
		) != 0
	) {
		LOG_E(usb_accessory_worker_log_tag, "Can`t setup heartbeat thread");

		goto out;
	}

	LOG_I(usb_accessory_worker_log_tag ,"Configured device started ...");

	while (TRUE) {
		usb_accessory_msg_processor_status_t status;

		if (usb_accessory_message_processor_handle(accessory_fd) == USB_ACCESSORY_MSG_OK) {
			usb_accessory_worker_heartbeat();
		}
	}

out:

	if (accessory_fd > 0)
		close(accessory_fd);

	return NULL;
}

void *usb_accessory_worker_heartbeat_thread(void *arg)
{
	usb_accessory_worker_heartbeat_context_t *heartbeat_context = (usb_accessory_worker_heartbeat_context_t*)arg;

	if (heartbeat_context == NULL) {
		return NULL;
	} 	

	while (1) {
		usleep(100);

		unsigned long elapsed_time = usb_accessory_worker_get_ts() - usb_accessory_worker_latest_activity_ts;

		if (elapsed_time > USB_ACCESSORY_WORKER_SEC_TO_US(USB_ACCESSORY_WORKER_USB_HEARTBEAT_INTERVAL_SEC)) {
			break;
		}
	}

	heartbeat_context->is_connection_was_lost = TRUE;

	video_receiver_remove_sink();
	pthread_cancel(heartbeat_context->worker_thread);
	close(heartbeat_context->accessory_fd);

	LOG_I(usb_accessory_worker_log_tag, "Connection lost");

	return NULL;
}

unsigned long usb_accessory_worker_get_ts(void) {
	struct timeval tv;
	
	gettimeofday(&tv, NULL);
	
	return USB_ACCESSORY_WORKER_SEC_TO_US(tv.tv_sec) + tv.tv_usec;
}

void usb_accessory_worker_heartbeat(void) {
	usb_accessory_worker_latest_activity_ts = usb_accessory_worker_get_ts();
}