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

#pragma mark - Extern definitions

extern usb_accessory_data_t usb_accessory_initial;
extern usb_accessory_data_t usb_accessory_configured;

#pragma mark - Internal methods

int usb_accessory_create(usb_accessory_data_t accessory_data);
int usb_accessory_remove_all(void);

#ifdef __cplusplus
}
#endif

#endif
