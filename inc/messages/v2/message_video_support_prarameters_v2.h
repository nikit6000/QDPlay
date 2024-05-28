#ifndef _MESSAGE_VIDEO_SUPPORT_PARAMETERS_V2_H_
#define _MESSAGE_VIDEO_SUPPORT_PARAMETERS_V2_H_

#include <stdint.h>
#include <stdlib.h>

#define MESSAGE_VIDEO_SUPPORT_FORMAT_H264       (3)

#pragma mark - Internal types

typedef struct {
    uint8_t video_format;
    uint8_t video_support;
} message_video_support_parameters_v2_t;

#pragma mark - Internal methods definition

int message_video_support_req_parameters_v2_init(const char* buffer, size_t len, message_video_support_parameters_v2_t *obj);
int message_video_support_rsp_parameters_v2_init(message_video_support_parameters_v2_t *obj, uint8_t **buffer, size_t *len);

#endif
