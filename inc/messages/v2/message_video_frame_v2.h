#ifndef _MESSAGE_VIDEO_FRAME_V2_H_
#define _MESSAGE_VIDEO_FRAME_V2_H_

#include <stdint.h>
#include <stdlib.h>
#include "messages/v2/message_common_header_v2.h"

#pragma mark - Internal types

typedef struct {
    uint16_t ext_size;
    uint8_t ext_type;
    uint8_t res;
    uint32_t width;
    uint32_t height;
    uint16_t orientation;
    uint8_t land;
    uint8_t enc_type;
    uint32_t framerate;
    uint32_t bitrate;
    uint32_t frame_interval;
    uint8_t in_out_app;
    uint8_t unk[3];
} __attribute__((packed)) message_video_frame_ext_v2_t;

typedef struct {
    message_common_header_v2_t header;
    message_video_frame_ext_v2_t ext;
} __attribute__((packed)) message_video_frame_v2_t;

#pragma mark - Internal methods definition

void message_video_frame_v2_init(uint32_t width, uint32_t height, uint32_t framerate, message_video_frame_v2_t *obj);

#endif
