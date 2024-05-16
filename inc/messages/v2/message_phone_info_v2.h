#ifndef _MESSAGE_PHONE_INFO_V2_H_
#define _MESSAGE_PHONE_INFO_V2_H_

#include <stdlib.h>
#include <stdint.h>

#pragma mark - Internal function definitions

int message_phone_info_v2_init(
    uint16_t width,
    uint16_t height,
    uint8_t ** buffer, 
    size_t *len
);

#endif