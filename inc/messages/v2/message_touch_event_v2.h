#ifndef _MESSAGE_TOUCH_EVENT_V2_
#define _MESSAGE_TOUCH_EVENT_V2_

#include <stdint.h>
#include <stdlib.h>
#include "messages/v2/message_common_header_v2.h"

#pragma mark - Internal definitions

#define MESSAGE_TOUCH_FINGER_V2_ACTION_DOWN         (1)
#define MESSAGE_TOUCH_FINGER_V2_ACTION_UP           (2)
#define MESSAGE_TOUCH_FINGER_V2_ACTION_MOVE         (3)

#define MESSAGE_TOUCH_V2_MAX_FINGERS                (10)

#pragma mark - Internal types

typedef struct {
    uint8_t finger_id;
    uint8_t finger_action;
    float x;
    float y;
} message_touch_finger_v2_t;

typedef struct {
    uint32_t action_id;
    message_touch_finger_v2_t fingers[MESSAGE_TOUCH_V2_MAX_FINGERS];
    size_t fingers_count;
} message_touches_v2_t;

#pragma mark - Internal methods definition

// Parses the touch payload (the bytes after the common and extended headers).
// Returns 0 on success, negative errno otherwise.
int message_touch_v2_parse(const uint8_t *payload, size_t payload_len, message_touches_v2_t *touches);

#endif
