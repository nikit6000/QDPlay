#include <string.h>
#include <errno.h>
#include <byteswap.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_video_support_prarameters_v2.h"

#pragma mark - Internal methods

int message_video_support_req_parameters_v2_init(const char* buffer, size_t len, message_video_support_parameters_v2_t *obj) {
    cJSON *root_object = NULL;
    cJSON *parameters_object = NULL;
    cJSON *video_format_item = NULL;

    if (obj == NULL) {
        return -EINVAL;
    }

    root_object = cJSON_ParseWithLength((const char*)buffer, len);

    if (root_object == NULL) {
        return -EINVAL;
    }

    parameters_object = cJSON_GetObjectItem(root_object, MESSAGE_V2_JSON_PARAMETERS_KEY);

    if (root_object == NULL) {
        cJSON_Delete(root_object);

        return -EINVAL;
    }

    video_format_item = cJSON_GetObjectItem(parameters_object, "VideoFormat");

    if (video_format_item == NULL || !cJSON_IsNumber(video_format_item)) {
        cJSON_Delete(root_object);

        return -EINVAL;
    }

    obj->video_format = cJSON_GetNumberValue(video_format_item);

    cJSON_Delete(root_object);

    return 0;
}

int message_video_support_rsp_parameters_v2_init(message_video_support_parameters_v2_t *obj, uint8_t **buffer, size_t *len) {
    message_common_header_v2_t *header = NULL;
    cJSON *root_object = NULL;
    cJSON *parameters_object = NULL;
    uint32_t full_message_len = 0;
    char* json_string = NULL;

    if (obj == NULL || buffer == NULL || len == NULL) {
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

    if (cJSON_AddNumberToObject(parameters_object, "VideoFormat", obj->video_format) == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    if (cJSON_AddNumberToObject(parameters_object, "VideoSupport", obj->video_support) == NULL) {
        cJSON_Delete(root_object);

        return -ENOMEM;
    }

    if (cJSON_AddStringToObject(root_object, MESSAGE_V2_JSON_CMD_KEY, MESSAGE_V2_JSON_CMD_VIDEO_SUP_RSP) == NULL) {
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
