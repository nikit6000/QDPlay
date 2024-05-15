#include <stdlib.h>
#include <stdio.h>
#include "usb_accessory/usb_accessory.h"
#include "usb_accessory/usb_accessory_worker.h"
#include "services/video_receiver/video_receiver.h"
#include "services/messaging_service/messaging_service.h"

int main(void) {

	if (video_receiver_start() != VIDEO_RECEIVER_OK) {
		return EXIT_FAILURE;
	}

	if (messaging_service_init() != TRUE) {
		return EXIT_FAILURE;
	}

	if (usb_accessory_create_initial_gadget() < 0) {
		return EXIT_FAILURE;
	}

	return usb_accessory_worker_start();
}