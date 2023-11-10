#ifndef _USB_ACCESSORY_MESSAGE_PROCESSOR_H_
#define _USB_ACCESSORY_MESSAGE_PROCESSOR_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Types

typedef enum {
	USB_ACCESSORY_MSG_OK = 0,
	USB_ACCESSORY_MSG_ERR_NO_MEM,
	USB_ACCESSORY_MSG_ERR_BROKEN_FD,
	USB_ACCESSORY_MSG_ERR_IO,
} usb_accessory_msg_processor_status_t;

#pragma mark - Internal methods definitions

usb_accessory_msg_processor_status_t usb_accessory_message_processor_setup(int acccessory_fd);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle(int acccessory_fd);

#ifdef __cplusplus
}
#endif

#endif /* _USB_ACCESSORY_MESSAGE_PROCESSOR_H_ */
