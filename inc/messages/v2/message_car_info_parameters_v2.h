#ifndef _MESSAGE_CAR_INFO_PARAMETERS_V2_H_
#define _MESSAGE_CAR_INFO_PARAMETERS_V2_H_

#include <stdint.h>

#pragma mark - Internal types

typedef struct {
    uint16_t width;
    uint16_t height;
} message_car_info_parameters_v2_t;

#pragma mark - Internal methods definition

int message_car_info_parameters_v2_init(uint8_t* buffer, size_t len, message_car_info_parameters_v2_t *obj);

#endif
