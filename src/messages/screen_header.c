#include <string.h>
#include <netinet/in.h>
#include "messages/screen_header.h"
#include "macros/data_types.h"
#include "macros/comand_types.h"

#pragma mark - Internal methods implementations

void screen_header_response_init(
    screen_header_t * obj,
    uint32_t width,
    uint32_t height,
    uint32_t frame_rate,
    uint32_t frame_interval
) {
    if (!obj) {
        return;
    }

    memcpy(obj->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN);
    obj->common_header.data_type = ntohl(MESSAGE_DATA_TYPE_SCREEN_CAPTURE);
    obj->common_header.header_size = ntohl(512);
    obj->common_header.common_header_size = ntohl(64);
    obj->common_header.request_header_size = ntohl(128);
    obj->common_header.response_header_size = ntohl(128);
    obj->common_header.action = ntohl(1);

    obj->capture_width = ntohl(width);
    obj->capture_height = ntohl(height);
    obj->pixel_format = ntohl(4);
    obj->pixel_size = ntohl(2);
    obj->encoding_type = ntohl(SCREEN_HEADER_VIDEO_FORMAT_H264); 
    obj->screen_orientation = ntohl(0);
    obj->screen_direct = ntohl(1);
    obj->no = ntohl(0);
    obj->screen_width = ntohl(width);
    obj->screen_height = ntohl(height);
    obj->screen_pixel_format = ntohl(0);
    obj->screen_pixel_size = ntohl(0);
    obj->screen_capture_width = ntohl(width);
    obj->screen_capture_height = ntohl(height);
    obj->screen_capture_pixel_format = ntohl(0);
    obj->screen_captire_pixel_size = ntohl(0);
    obj->screen_capture_encoding_type = ntohl(SCREEN_HEADER_VIDEO_FORMAT_H264);
    obj->screen_capture_orientation = ntohl(90);
    obj->screen_capture_direct = ntohl(90);
    obj->screen_capture_number_of_screens = ntohl(0);
    obj->screen_width_0 = ntohl(width);
    obj->screen_height_0 = ntohl(height);
    obj->screen_width_1 = ntohl(0);
    obj->screen_height_1 = ntohl(0);
    obj->screen_width_2 = ntohl(0);
    obj->screen_height_2 = ntohl(0);
    obj->screen_width_3 = ntohl(0);
    obj->screen_height_3 = ntohl(0);
    obj->frame_rate = ntohl(frame_rate);
    obj->bit_rate = ntohl(0);
    obj->frame_interval = ntohl(frame_interval);
    obj->capture_frame_rate = ntohl(frame_rate);
    obj->capture_bit_rate = ntohl(0);
    obj->capture_frame_interval = ntohl(frame_interval);
    obj->in_out_app = ntohl(1);

    for (int i = 0; i < 32; i++) {
        obj->common_header.mark[i] = i + 32;
    }
}