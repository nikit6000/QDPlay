#ifndef _MESSAGE_UPDATE_STATUS_V2_H_
#define _MESSAGE_UPDATE_STATUS_V2_H_

#include <stdlib.h>
#include <stdint.h>

#pragma mark - Internal function definitions

int message_update_satus_v2_init(
    uint8_t update_status,
    uint8_t ** buffer, 
    size_t *len
);

#endif