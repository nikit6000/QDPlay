#ifndef _USB_ACTIVE_ACCESSORY_H_
#define _USB_ACTIVE_ACCESSORY_H_

#include <stdlib.h>
#include <signal.h>
#include "message_processor/message_processor.h"

#pragma mark - Internal function definitions

// Signal used to kick the session thread out of a blocking read(). Installed by
// usb_active_accessory_install_signal_handler() without SA_RESTART.
#define USB_ACTIVE_ACCESSORY_WAKEUP_SIGNAL      SIGUSR1

void usb_active_accessory_install_signal_handler(void);

// Switches the gadget to the accessory identity and serves one head unit session.
// Returns when the session is over (head unit gone, timeout, video sources gone).
int usb_active_accessory_run_session(void);

// I/O for the message processors. Reads are lossless for any length (see the bounce buffer).
int usb_active_accessory_read(void *buffer, size_t len);
int usb_active_accessory_read_message_tail(uint8_t *buffer, size_t received, size_t required, size_t capacity);
int usb_active_accessory_write(void *buffer, size_t len);
int usb_active_accessory_write_h264(message_processor_video_params_t video_parameters, void *buffer, size_t len);

int usb_active_accessory_write_with_padding_512(
    void *header,
    size_t header_len,
    void *body,
    size_t body_len
);

#endif
