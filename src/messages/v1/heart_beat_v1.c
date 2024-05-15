#include <stdlib.h>
#include <string.h>
#include "byteswap.h"
#include "macros/comand_types.h"
#include "macros/data_types.h"
#include "messages/v1/heart_beat_v1.h"

#pragma mark - Internal methods

void heart_beat_v1_init(heart_beat_v1_t* obj) {
    if (obj == NULL) {
        return;
    }

    memset(obj, 0, sizeof(heart_beat_v1_t));
    memcpy(obj->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN);

    obj->common_header.data_type = bswap_32(MESSAGE_DATA_TYPE_CMD);
    obj->common_header.header_size = bswap_32(512);
    obj->common_header.total_size = bswap_32(512);
    obj->common_header.common_header_size = bswap_32(64);
    obj->common_header.request_header_size = bswap_32(128);
    obj->common_header.response_header_size = bswap_32(128);
    obj->common_header.action = bswap_32(1);
    
    obj->common_command_header.cmd = bswap_32(MESSAGE_CMD_HEARTBEAT);
    obj->value = bswap_32(1);
    obj->ret = bswap_32(1);

    for (int i = 0; i < 32; i++) {
        obj->common_header.mark[i] = i + 32;
    }
}
