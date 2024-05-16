#include <errno.h>
#include <string.h>
#include <byteswap.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_update_status_v2.h"

#pragma mark - Internal methods

int message_update_satus_v2_init(
    uint8_t update_status,
    uint8_t ** buffer, 
    size_t *len
) {
    message_common_header_v2_t *header = NULL;
    char *json_string = NULL;
    cJSON *root_object = NULL;
    cJSON *parameters_object = NULL;
    cJSON *phone_feature_object = NULL;
    uint32_t full_message_len = 0;
    
    if (buffer == NULL || len == NULL) {
        return -EINVAL;
    }

    root_object = cJSON_CreateObject();

    if (root_object == NULL) {
        return -ENOMEM;
    }

    parameters_object = cJSON_AddObjectToObject(root_object, MESSAGE_V2_JSON_PARAMETERS_KEY);

    if (parameters_object == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    if (cJSON_AddNumberToObject(parameters_object, "UpdateStatus", 5) == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    if (cJSON_AddStringToObject(root_object, MESSAGE_V2_JSON_CMD_KEY, MESSAGE_V2_JSON_CMD_UPDATE_NOTIFY) == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    json_string = cJSON_PrintUnformatted(root_object);

    cJSON_Delete(root_object);

    if (json_string == NULL) {
        return -ENOMEM;
    }

    full_message_len = sizeof(message_common_header_v2_t) + strlen(json_string);
    *buffer = (uint8_t*)malloc(full_message_len);

    if (*buffer == NULL) {
        free(json_string);

        return -ENOMEM;
    }

    memset(*buffer, 0, full_message_len);

    header = (message_common_header_v2_t*)*buffer;

    strncpy(header->mark, MESSAGE_COMMON_HEADER_V2_MARK, sizeof(header->mark));

    header->full_message_len = bswap_32(full_message_len);
    header->payload_format = MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON;

    memcpy(*buffer + sizeof(message_common_header_v2_t), json_string, strlen(json_string));

    free(json_string);

    *len = full_message_len;

    return 0;
}
