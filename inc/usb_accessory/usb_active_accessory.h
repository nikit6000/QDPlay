#ifndef _USB_ACTIVE_ACCESSORY_H_
#define _USB_ACTIVE_ACCESSORY_H_

#include <stdlib.h>
#include "message_processor/message_processor.h"

#pragma mark - Internal function definitions

int usb_active_accessory_create_and_wait(const char* device_path);
int usb_active_accessory_read(void *buffer, size_t len);
int usb_active_accessory_write(void *buffer, size_t len);
int usb_active_accessory_write_h264(message_processor_video_params_t video_parameters, void *buffer, size_t len);

#endif
