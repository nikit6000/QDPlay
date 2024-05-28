#ifndef _MESSAGE_TOUCH_EVENT_V2_
#define _MESSAGE_TOUCH_EVENT_V2_

#include "messages/v2/message_common_header_v2.h"

#pragma mark - Internal definitions

#define MESSAGE_TOUCH_FINGER_V2_ACTION_DOWN         (1)
#define MESSAGE_TOUCH_FINGER_V2_ACTION_UP           (2)
#define MESSAGE_TOUCH_FINGER_V2_ACTION_MOVE         (3)

#pragma mark - Internal types

typedef struct {
    uint8_t finger_id;
    uint8_t finger_action;
    float x;
    float y;
} message_touch_finger_v2_t;

typedef struct {
    uint32_t action_id;
    message_touch_finger_v2_t **fingers;
    size_t fingers_count;
} message_touches_v2_t;

#pragma mark - Internal methods definition

message_touches_v2_t *  message_touch_v2_init(void *message, size_t message_len);
void                    message_touch_v2_free(message_touches_v2_t *touches);

#endif
