#include <errno.h>
#include <string.h>
#include <byteswap.h>
#include "messages/v2/message_common_header_v2.h"

#pragma mark - Private properties

const char* new_message_heartbeat_data = "{\"CMD\":\"HEARTBEAT\"}";

#pragma mark - Internal function implementations

int new_message_heartbeat_init(uint8_t* buffer, size_t len) {
    size_t total_len = sizeof(message_common_header_v2_t) + strlen(new_message_heartbeat_data);
    message_common_header_v2_t *header = (message_common_header_v2_t*)buffer;

    if (header == NULL) {
        return -EINVAL;
    }

    if (len < total_len) {
        return -EINVAL;
    }
    
    memset(header, 0, total_len);

    strncpy(header->mark, MESSAGE_NEW_HEADER, sizeof(header->mark));

    header->full_message_len = bswap_32(total_len);
    header->payload_format = 1;

    strcpy(buffer + sizeof(message_common_header_v2_t), new_message_heartbeat_data);

    return 0;
}
