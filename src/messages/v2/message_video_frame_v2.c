#include <byteswap.h>
#include <string.h>
#include "messages/v2/message_video_frame_v2.h"

#pragma mark - Internal methods definition

void message_video_frame_v2_init(
    uint32_t width, 
    uint32_t height, 
    uint32_t framerate, 
    message_video_frame_v2_t *obj
) {
    if (obj == NULL) {
        return;
    }

    strncpy(obj->header.mark, MESSAGE_COMMON_HEADER_V2_MARK, sizeof(obj->header.mark));

    obj->header.extended_header_size = bswap_16(sizeof(message_video_frame_ext_v2_t));
    obj->header.msg_type = 0x01;
    obj->header.source = 0x00;
    obj->header.destination = 0x00;
    obj->header.payload_format = MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_VIDEO;
    obj->header.reserved = 0x00;
    obj->header.unk0 = 0x00;

    obj->ext.ext_size = bswap_16(sizeof(message_video_frame_ext_v2_t));
    obj->ext.ext_type = 0x01;
    obj->ext.res = 0x00;
    obj->ext.width = bswap_32(width);
    obj->ext.height = bswap_32(height);
    obj->ext.orientation = bswap_16(0x5A);
    obj->ext.land = 0x01;
    obj->ext.enc_type = 0x03;
    obj->ext.framerate = bswap_32(framerate);
    obj->ext.bitrate = bswap_32(0x00);
    obj->ext.frame_interval = bswap_32(0x00);
    obj->ext.in_out_app = 0x01;
    obj->ext.unk[0] = 0x00;
    obj->ext.unk[1] = 0x00;
    obj->ext.unk[2] = 0x00;
}