#include <string.h>
#include <netinet/in.h>
#include "messages/mirror_support.h"
#include "macros/data_types.h"
#include "macros/comand_types.h"

#pragma mark - Internal methods implementations

void mirror_support_response_init(mirror_support_t * obj) {
    if (!obj) {
        return;
    }

    memcpy(obj->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN);
    obj->common_header.data_type = ntohl(MESSAGE_DATA_TYPE_CMD);
    obj->common_header.header_size = ntohl(512);
    obj->common_header.total_size = ntohl(512);
    obj->common_header.common_header_size = ntohl(64);
    obj->common_header.request_header_size = ntohl(128);
    obj->common_header.response_header_size = ntohl(128);
    obj->common_header.action = ntohl(1);
    obj->common_command_header.time_stamp = ntohl(0);
    obj->common_command_header.cmd = ntohl(MESSAGE_CMD_MIRROR_SUPPORT);
    obj->ret = ntohl(1);
    obj->screen_capture_support = ntohl(0);

    for (int i = 0; i < 32; i++) {
        obj->common_header.mark[i] = i + 32;
    }
}