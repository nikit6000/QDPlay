#include <string.h>
#include <errno.h>
#include "utils/cJSON.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_cmd_response_v2.h"

#pragma mark - Internal methods

int message_cmd_response_v2_init(const char* buffer, size_t len, message_cmd_response_v2_t *obj) {
    cJSON *root_object = NULL;
    cJSON *cmd_item = NULL;

    if (obj == NULL) {
        return -EINVAL;
    }

    root_object = cJSON_ParseWithLength(buffer, len);

    if (root_object == NULL) {
        return -EINVAL;
    }

    cmd_item = cJSON_GetObjectItem(root_object, MESSAGE_V2_JSON_CMD_KEY);

    if (cmd_item == NULL || !cJSON_IsString(cmd_item)) {
        cJSON_Delete(root_object);

        return -EINVAL;
    }

    strncpy(obj->cmd, cJSON_GetStringValue(cmd_item), sizeof(obj->cmd) - 1);

    cJSON_Delete(root_object);

    return 0;
}