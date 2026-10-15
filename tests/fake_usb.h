#ifndef _FAKE_USB_H_
#define _FAKE_USB_H_

#include <stdint.h>
#include <stdlib.h>

void fake_usb_reset(void);
void fake_usb_queue_transfer(const void *data, size_t len);
size_t fake_usb_tx(const uint8_t **data);
int fake_usb_h264_frames(void);

#endif
