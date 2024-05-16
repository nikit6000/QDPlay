#include <errno.h>
#include <string.h>
#include <byteswap.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_phone_info_v2.h"
#include "messages/v2/message_car_info_parameters_v2.h"

#pragma mark - Internal methods

int message_car_info_parameters_v2_init(uint8_t* buffer, size_t len, message_car_info_parameters_v2_t *obj) {
    cJSON *root_object = NULL;
    cJSON *parameters_object = NULL;
    cJSON *width_item = NULL;
    cJSON *height_item = NULL;
    
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

    width_item = cJSON_GetObjectItem(parameters_object, "CarWidth");
    height_item = cJSON_GetObjectItem(parameters_object, "CarHeight");

    if (width_item == NULL || height_item == NULL) {
        cJSON_Delete(root_object);

        return -EINVAL;
    }

    obj->width = cJSON_GetNumberValue(width_item);
    obj->height = cJSON_GetNumberValue(height_item);

    return 0;
}
