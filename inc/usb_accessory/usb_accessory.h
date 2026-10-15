#ifndef _USB_ACCESSORY_H_
#define _USB_ACCESSORY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Types

typedef struct {
    uint16_t vid;
    uint16_t pid;
    const char* serial;
    const char* manufacturer;
    const char* product;
} usb_accessory_data_t;

typedef enum {
    // Looks like an ordinary Android phone: the head unit starts the AOA handshake
    USB_ACCESSORY_MODE_INITIAL = 0,
    // Google accessory VID/PID: the head unit opens the bulk endpoints
    USB_ACCESSORY_MODE_ACCESSORY
} usb_accessory_mode_t;

#pragma mark - Internal methods

// Creates the configfs gadget with the accessory function (removing a stale one).
int usb_accessory_init(void);

// Re-enumerates the gadget with the identity of the given mode.
int usb_accessory_set_mode(usb_accessory_mode_t mode);

// Detaches the gadget from the UDC: the head unit sees a disconnect.
int usb_accessory_disable(void);

#ifdef __cplusplus
}
#endif

#endif
