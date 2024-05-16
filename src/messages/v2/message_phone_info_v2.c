#include <errno.h>
#include <string.h>
#include <byteswap.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_phone_info_v2.h"

#pragma mark - Private definitions

#define MESSAGE_PHONE_INFO_ASSUME_NON_NULL(x)              do {        \
    if (x == NULL) {                                                   \
        cJSON_Delete(root_object);                                     \
        return -ENOMEM;                                                \
    }                                                                  \
} while(0)

#pragma mark - Internal methods

int message_phone_info_v2_init(
    uint16_t width,
    uint16_t height,
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

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        parameters_object
    );

    phone_feature_object = cJSON_AddObjectToObject(parameters_object, "PhoneFeature");

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        phone_feature_object
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(phone_feature_object, "PassistMobileNum", "")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(parameters_object, "PhoneName", "")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(parameters_object, "PlatformVersion", "31")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(parameters_object, "PhoneModel", "Samsung Galaxy A50")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "PhoneSystemTime", 0)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "PhoneHeightInApp", height)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "PhoneWidthInApp", width)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "MirrorHeightInApp", height)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "MirrorWidthInApp", width)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "MirrorTypeSupport", 2)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(parameters_object, "PhoneUUID", "")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(parameters_object, "Version", "1.7.7")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "MirrorHeight", height)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "MirrorWidth", width)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "PhoneHeight", height)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "PhoneWidth", width)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddNumberToObject(parameters_object, "Platform", 0)
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(parameters_object, "PhoneBrand", "Samsung")
    );

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        cJSON_AddStringToObject(root_object, MESSAGE_V2_JSON_CMD_KEY, MESSAGE_V2_JSON_CMD_PHONE_INFO)
    );

    json_string = cJSON_PrintUnformatted(root_object);

    MESSAGE_PHONE_INFO_ASSUME_NON_NULL(
        json_string
    );

    cJSON_Delete(root_object);

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