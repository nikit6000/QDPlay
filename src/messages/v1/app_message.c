#include <string.h>
#include <netinet/in.h>
#include "messages/v1/app_message.h"
#include "macros/data_types.h"

#pragma mark - Internal methods implementations

void app_message_response_init(app_message_t * obj) {
    if (!obj) {
        return;
    }

    memcpy(obj->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN);
    obj->common_header.data_type = ntohl(MESSAGE_DATA_TYPE_HU_MSG);
    obj->common_header.header_size = ntohl(512);
    obj->common_header.total_size = ntohl(512);
    obj->common_header.common_header_size = ntohl(64);
    obj->common_header.request_header_size = ntohl(64);
    obj->common_header.response_header_size = ntohl(0);
    obj->common_header.action = ntohl(2);

    for (int i = 0; i < 32; i++) {
        obj->common_header.mark[i] = i + 32;
    }
}