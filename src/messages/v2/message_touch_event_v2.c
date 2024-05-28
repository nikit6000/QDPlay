#include <byteswap.h>
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

#pragma mark - Internal methods

message_touches_v2_t * message_touch_v2_init(void *message, size_t message_len) {
    message_common_header_v2_t* header = NULL;
    message_touches_v2_t* touches = NULL;
    message_touch_event_ext_v2_t* touches_payload = NULL;
    uint32_t full_message_len = 0;
    uint32_t payload_len = 0;

    if (message == NULL || message_len < sizeof(message_common_header_v2_t)) {
        return NULL;
    }

    header = (message_common_header_v2_t*)message;

    if (header->msg_type != MESSAGE_COMMON_HEADER_V2_MSG_TYPE_TOUCH) {
        return NULL;
    }

    if (header->source != MESSAGE_COMMON_HEADER_V2_SOURCE_CAR) {
        return NULL;
    }

    full_message_len = bswap_32(header->full_message_len);

    if (message_len < full_message_len) {
        return NULL;
    }

    if (full_message_len <= sizeof(message_common_header_v2_t)) {
        return NULL;
    }

    payload_len = full_message_len - sizeof(message_common_header_v2_t) - sizeof(message_touch_event_ext_v2_t);
    
    message += sizeof(message_common_header_v2_t);
    message_len -= sizeof(message_common_header_v2_t);

    touches_payload = (message_touch_event_ext_v2_t*)message;

    message += sizeof(message_touch_event_ext_v2_t);
    message_len -= sizeof(message_touch_event_ext_v2_t);

    touches = (message_touches_v2_t*)malloc(sizeof(message_touches_v2_t));

    if (touches == NULL) {
        return NULL;
    }
    
    touches->action_id = bswap_32(touches_payload->action_id);
    touches->fingers_count = touches_payload->touches_count;

    if ((payload_len / touches->fingers_count) != sizeof(message_touch_event_finger_v2_t)) {
        message_touch_v2_free(touches);

        return NULL;
    }

    touches->fingers = (message_touch_finger_v2_t**)malloc(touches->fingers_count * sizeof(message_touch_finger_v2_t*));

    if (touches->fingers == NULL) {
        message_touch_v2_free(touches);

        return NULL;
    }

    for (size_t i = 0; i < touches->fingers_count; i++) {
        message_touch_event_finger_v2_t *finger = (message_touch_event_finger_v2_t*)message;
        message_touch_finger_v2_t *mapped_finger = (message_touch_finger_v2_t*)malloc(sizeof(message_touch_finger_v2_t));
        uint32_t x = bswap_32(finger->x);
        uint32_t y = bswap_32(finger->y);

        if (mapped_finger == NULL) {
            message_touch_v2_free(touches);

            return NULL;
        }

        mapped_finger->finger_action = finger->finger_action;
        mapped_finger->finger_id = finger->finger_id;
        mapped_finger->x = *((float*)(&x));
        mapped_finger->y = *((float*)(&y));

        touches->fingers[i] = mapped_finger;

        message += sizeof(message_touch_event_finger_v2_t);
        message_len -= sizeof(message_touch_event_finger_v2_t);
    }

    return touches;
}

void message_touch_v2_free(message_touches_v2_t *touches) {

    if (touches == NULL) {
        return;
    }

    if (touches->fingers) {
        for (size_t i = 0; i < touches->fingers_count; i++) {
            free(touches->fingers[i]);
        }

        free(touches->fingers);
    }

    free(touches);
}
