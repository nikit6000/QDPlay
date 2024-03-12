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
    char* serial;
    char* manufacturer;
    char* product; 
} usb_accessory_data_t;

#pragma mark - Internal methods

int usb_accessory_create_initial_gadget(void);
int usb_accessory_reset(void);
int usb_accessory_configure(void);

#ifdef __cplusplus
}
#endif

#endif
