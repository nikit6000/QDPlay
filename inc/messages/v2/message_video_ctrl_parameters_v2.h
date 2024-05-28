#ifndef _MESSAGE_VIDEO_CTRL_PARAMETERS_V2_H_
#define _MESSAGE_VIDEO_CTRL_PARAMETERS_V2_H_

#include <stdint.h>
#include <stdlib.h>

#pragma mark - Internal types

typedef struct {
    uint8_t play_status;
} message_video_ctrl_parameters_v2_t;

#pragma mark - Internal methods definition

int message_video_ctrl_parameters_v2_init(const char* buffer, size_t len, message_video_ctrl_parameters_v2_t *obj);

#endif
