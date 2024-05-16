#include <errno.h>
#include <string.h>
#include <byteswap.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"

#pragma mark - Internal function implementations

int message_heart_beat_v2_init(uint8_t* buffer, size_t len) {
    message_common_header_v2_t *header = NULL;
    uint32_t total_len = 0;
    cJSON *root_object = NULL;
    char *json_string = NULL;

    header = (message_common_header_v2_t*)buffer;
    root_object = cJSON_CreateObject();

    if (buffer == NULL || root_object == NULL) {
        return -EINVAL;
    }

    if (cJSON_AddStringToObject(root_object, MESSAGE_V2_JSON_CMD_KEY, MESSAGE_V2_JSON_CMD_HEARTBEAT) == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    json_string = cJSON_PrintUnformatted(root_object);

    if (json_string == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    total_len = sizeof(message_common_header_v2_t) + strlen(json_string);

    cJSON_Delete(root_object);

    if (len < total_len) {
        free(json_string);

        return -EINVAL;
    }
    
    memset(header, 0, total_len);

    strncpy(header->mark, MESSAGE_COMMON_HEADER_V2_MARK, sizeof(header->mark));

    header->full_message_len = bswap_32(total_len);
    header->payload_format = MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON;

    strcpy(buffer + sizeof(message_common_header_v2_t), json_string);

    free(json_string);

    return 0;
}
