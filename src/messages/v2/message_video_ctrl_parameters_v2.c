#include <string.h>
#include <errno.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_video_ctrl_parameters_v2.h"

#pragma mark - Internal methods definition

int message_video_ctrl_parameters_v2_init(const char* buffer, size_t len, message_video_ctrl_parameters_v2_t *obj) {
    cJSON *root_object = NULL;
    cJSON *parameters_object = NULL;
    cJSON *play_status_item = NULL;

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

    play_status_item = cJSON_GetObjectItem(parameters_object, "PlayStatus");

    if (play_status_item == NULL || !cJSON_IsNumber(play_status_item)) {
        cJSON_Delete(root_object);

        return -EINVAL;
    }

    obj->play_status = cJSON_GetNumberValue(play_status_item);

    cJSON_Delete(root_object);

    return 0;
}