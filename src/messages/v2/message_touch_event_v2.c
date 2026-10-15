#include <byteswap.h>
#include <errno.h>
#include <string.h>
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_touch_event_v2.h"

#pragma mark - Private types

typedef struct {
    uint8_t finger_id;
    uint8_t finger_action;
    uint32_t x;
    uint32_t y;
} __attribute__((packed)) message_touch_event_finger_v2_t;

typedef struct {
    uint32_t action_id;
    uint8_t touches_count;
} __attribute__((packed)) message_touch_event_ext_v2_t;

#pragma mark - Private methods

static float message_touch_v2_be_float(uint32_t raw) {
    uint32_t value = bswap_32(raw);
    float result;

    memcpy(&result, &value, sizeof(result));

    return result;
}

#pragma mark - Internal methods

int message_touch_v2_parse(const uint8_t *payload, size_t payload_len, message_touches_v2_t *touches) {
    const message_touch_event_ext_v2_t *touches_payload = NULL;
    size_t fingers_count = 0;

    if (payload == NULL || touches == NULL || payload_len < sizeof(message_touch_event_ext_v2_t)) {
        return -EINVAL;
    }

    touches_payload = (const message_touch_event_ext_v2_t*)payload;
    fingers_count = touches_payload->touches_count;

    // Same layout as in the phone application: 5 bytes of header + 10 bytes per finger.
    // Trailing bytes are USB padding and are ignored.

    if (fingers_count == 0 ||
        fingers_count > MESSAGE_TOUCH_V2_MAX_FINGERS ||
        payload_len < sizeof(message_touch_event_ext_v2_t) + fingers_count * sizeof(message_touch_event_finger_v2_t)) {
        return -EINVAL;
    }

    memset(touches, 0, sizeof(*touches));

    touches->action_id = bswap_32(touches_payload->action_id);
    touches->fingers_count = fingers_count;

    const message_touch_event_finger_v2_t *finger = (const message_touch_event_finger_v2_t*)(payload + sizeof(message_touch_event_ext_v2_t));

    for (size_t i = 0; i < fingers_count; i++, finger++) {
        touches->fingers[i].finger_id = finger->finger_id;
        touches->fingers[i].finger_action = finger->finger_action;
        touches->fingers[i].x = message_touch_v2_be_float(finger->x);
        touches->fingers[i].y = message_touch_v2_be_float(finger->y);
    }

    return 0;
}
