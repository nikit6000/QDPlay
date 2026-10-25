#include <glib.h>
#include <string.h>
#include <errno.h>
#include <byteswap.h>
#include "utils/cJSON.h"
#include "logging/logging.h"
#include "services/messaging_service/messaging_service.h"
#include "services/video_receiver/video_receiver.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_heart_beat_v2.h"
#include "messages/v2/message_phone_info_v2.h"
#include "messages/v2/message_car_info_parameters_v2.h"
#include "messages/v2/message_cmd_response_v2.h"
#include "messages/v2/message_video_ctrl_parameters_v2.h"
#include "messages/v2/message_video_support_prarameters_v2.h"
#include "messages/v2/message_video_frame_v2.h"
#include "messages/v2/message_update_status_v2.h"
#include "messages/v2/message_touch_event_v2.h"
#include "usb_accessory/usb_active_accessory.h"
#include "usb_accessory/message_processor/v2/message_processor_v2.h"

#pragma mark - Private definitions

#define MESSAGE_PROCESSOR_V2_CHUNK_SIZE                 (512)
#define MESSAGE_PROCESSOR_V2_CHUNK_SIZE_LOG2            (9)
// Control messages are small JSON documents; anything bigger is a broken stream
#define MESSAGE_PROCESSOR_V2_MAX_MESSAGE_SIZE           (1024 * 1024)
// Same value as in the phone application
#define MESSAGE_PROCESSOR_V2_READ_TIMEOUT_SEC           (5)
// Touching the screen with this many fingers switches between video sources
#define MESSAGE_PROCESSOR_V2_SWITCH_GESTURE_FINGERS     (3)

#pragma mark - Private types

typedef struct {
    message_common_header_v2_t *header;
    uint8_t *payload;
    size_t payload_len;
} message_processor_v2_message_t;

#pragma mark - Private methods definition

static gboolean    message_processor_v2_probe(uint8_t * buffer, size_t len);
static gboolean    message_processor_v2_run(void);
static gboolean    message_processor_v2_write_hb(void);
static int         message_processor_v2_write_h264(message_processor_video_params_t video_parameters, void * buffer, size_t len);

static gboolean    message_processor_v2_read_message(uint8_t **buffer, size_t *capacity, size_t received, message_processor_v2_message_t *message);
static gboolean    message_processor_v2_handle_bin_msg(message_processor_v2_message_t *message);
static gboolean    message_processor_v2_handle_json_msg(const char* json, size_t len);
static gboolean    message_processor_v2_handle_video_ctrl(const char* json, size_t len);
static gboolean    message_processor_v2_answer_video_support_type(const char* json, size_t len);
static gboolean    message_processor_v2_send_json(uint8_t *message_buffer, size_t message_len);
static gboolean    message_processor_v2_handle_touch(const uint8_t* payload, size_t len);

#pragma mark - Private properties

const gchar* message_processor_v2_tag = "MessageProcessorV2";

const message_processor_t message_processor_v2 = {
    .probe = message_processor_v2_probe,
    .run = message_processor_v2_run,
    .write_hb = message_processor_v2_write_hb,
    .write_h264 = message_processor_v2_write_h264,
    .read_timeout_sec = MESSAGE_PROCESSOR_V2_READ_TIMEOUT_SEC
};

static gboolean message_processor_v2_gesture_fired = FALSE;

#pragma mark - Internal methods

const message_processor_t* message_processor_v2_get(void) {
   return &message_processor_v2;
}

#pragma mark - Private methods

static size_t message_processor_v2_round_up(size_t len) {
    return (len + MESSAGE_PROCESSOR_V2_CHUNK_SIZE - 1) & ~((size_t)MESSAGE_PROCESSOR_V2_CHUNK_SIZE - 1);
}

// `*buffer` already holds `received` bytes of a message. Reads the rest of it,
// growing the buffer when needed, and splits it into header and payload.
static gboolean message_processor_v2_read_message(uint8_t **buffer, size_t *capacity, size_t received, message_processor_v2_message_t *message) {
    message_common_header_v2_t *header = NULL;
    size_t full_message_len = 0;
    size_t ext_header_len = 0;

    if (received < sizeof(message_common_header_v2_t)) {
        return FALSE;
    }

    header = (message_common_header_v2_t*)*buffer;

    if (strncmp(header->mark, MESSAGE_COMMON_HEADER_V2_MARK, sizeof(header->mark)) != 0) {
        LOG_E(message_processor_v2_tag, "Broken sequence: bad mark");

        return FALSE;
    }

    full_message_len = bswap_32(header->full_message_len);

    if (full_message_len < sizeof(message_common_header_v2_t) || full_message_len > MESSAGE_PROCESSOR_V2_MAX_MESSAGE_SIZE) {
        LOG_E(message_processor_v2_tag, "Bad message length: %zu", full_message_len);

        return FALSE;
    }

    if (full_message_len > received) {
        size_t required_capacity = message_processor_v2_round_up(full_message_len);

        if (required_capacity > *capacity) {
            uint8_t *resized_buffer = (uint8_t*)realloc(*buffer, required_capacity);

            if (resized_buffer == NULL) {
                return FALSE;
            }

            *buffer = resized_buffer;
            *capacity = required_capacity;
        }

        int total = usb_active_accessory_read_message_tail(*buffer, received, full_message_len, *capacity);

        if (total < 0) {
            LOG_E(message_processor_v2_tag, "Can`t read message tail (%zu bytes): %d", full_message_len - received, total);

            return FALSE;
        }

        header = (message_common_header_v2_t*)*buffer;
    }

    // JSON and binary payloads start after the extended header, as the phone application expects

    ext_header_len = bswap_16(header->extended_header_size);

    if (ext_header_len > full_message_len - sizeof(message_common_header_v2_t)) {
        LOG_E(message_processor_v2_tag, "Bad extended header length: %zu", ext_header_len);

        return FALSE;
    }

    message->header = header;
    message->payload = *buffer + sizeof(message_common_header_v2_t) + ext_header_len;
    message->payload_len = full_message_len - sizeof(message_common_header_v2_t) - ext_header_len;

    return TRUE;
}

static gboolean message_processor_v2_probe(uint8_t * buffer, size_t len) {
    message_processor_v2_message_t message = { 0 };
    message_cmd_response_v2_t cmd_response = { 0 };
    message_car_info_parameters_v2_t car_info = { 0 };
    message_common_header_v2_t *message_header = NULL;
    uint8_t * message_buffer = NULL;
    size_t capacity = 0;
    uint8_t * phone_info = NULL;
    size_t phone_info_len = 0;
    uint8_t * update_status = NULL;
    size_t update_status_len = 0;
    gboolean status = FALSE;

    if (len < sizeof(message_common_header_v2_t) || buffer == NULL) {
        return FALSE;
    }

    message_header = (message_common_header_v2_t*)buffer;

    if (strncmp(message_header->mark, MESSAGE_COMMON_HEADER_V2_MARK, sizeof(message_header->mark)) != 0 ||
        message_header->source != MESSAGE_COMMON_HEADER_V2_SOURCE_CAR ||
        message_header->payload_format != MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON) {
        return FALSE;
    }

    LOG_I(message_processor_v2_tag, "V2 format detected!");

    // Work on a private copy: the message may be longer than the probe buffer

    capacity = message_processor_v2_round_up(len);
    message_buffer = (uint8_t*)malloc(capacity);

    if (message_buffer == NULL) {
        return FALSE;
    }

    memcpy(message_buffer, buffer, len);

    do {
        if (message_processor_v2_read_message(&message_buffer, &capacity, len, &message) == FALSE) {
            break;
        }

        if (message_cmd_response_v2_init((const char*)message.payload, message.payload_len, &cmd_response) < 0 ||
            strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_CAR_INFO) != 0) {
            LOG_E(message_processor_v2_tag, "The first message is not CAR_INFO");

            break;
        }

        if (message_car_info_parameters_v2_init(message.payload, message.payload_len, &car_info) < 0) {
            LOG_E(message_processor_v2_tag, "Can`t parse CAR_INFO");

            break;
        }

        LOG_I(message_processor_v2_tag, "Got car info w: %d, h: %d", car_info.width, car_info.height);

        messaging_service_set_screen_size(car_info.width, car_info.height);

        if (message_phone_info_v2_init(car_info.width, car_info.height, &phone_info, &phone_info_len) < 0 ||
            message_processor_v2_send_json(phone_info, phone_info_len) == FALSE) {
            break;
        }

        if (message_update_satus_v2_init(5, &update_status, &update_status_len) < 0 ||
            message_processor_v2_send_json(update_status, update_status_len) == FALSE) {
            break;
        }

        status = TRUE;
    } while (0);

    free(phone_info);
    free(update_status);
    free(message_buffer);

    return status;
}

static gboolean message_processor_v2_run(void) {
    static uint8_t *message_buffer = NULL;
    static size_t message_buffer_size = 0;
    message_processor_v2_message_t message = { 0 };
    int received_data_len = 0;

    // The buffer is reused between calls: run() is executed for every message

    if (message_buffer == NULL) {
        message_buffer = (uint8_t*)malloc(MESSAGE_PROCESSOR_V2_CHUNK_SIZE);

        if (message_buffer == NULL) {
            return FALSE;
        }

        message_buffer_size = MESSAGE_PROCESSOR_V2_CHUNK_SIZE;
    }

    received_data_len = usb_active_accessory_read(message_buffer, MESSAGE_PROCESSOR_V2_CHUNK_SIZE);

    if (received_data_len <= 0) {
        return FALSE;
    }

    if (message_processor_v2_read_message(&message_buffer, &message_buffer_size, received_data_len, &message) == FALSE) {
        return FALSE;
    }

    if (message.header->source != MESSAGE_COMMON_HEADER_V2_SOURCE_CAR) {
        return TRUE;
    }

    if (message.header->payload_format == MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON) {
        return message_processor_v2_handle_json_msg((const char *)message.payload, message.payload_len);
    }

    if (message.header->payload_format == MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_BIN) {
        return message_processor_v2_handle_bin_msg(&message);
    }

    return TRUE;
}

static gboolean message_processor_v2_write_hb(void) {
    uint8_t message[MESSAGE_PROCESSOR_V2_CHUNK_SIZE];

    if (message_heart_beat_v2_init(message, MESSAGE_PROCESSOR_V2_CHUNK_SIZE) < 0) {
        return FALSE;
    }

    return usb_active_accessory_write(message, MESSAGE_PROCESSOR_V2_CHUNK_SIZE) > 0;
}

static int message_processor_v2_write_h264(message_processor_video_params_t video_parameters, void * buffer, size_t len) {
    // Only the video receiver thread sends frames, so a static header is safe here
    static message_video_frame_v2_t header = { 0 };
    uint32_t full_message_len = len + sizeof(message_video_frame_v2_t);

    message_video_frame_v2_init(
        video_parameters.width,
        video_parameters.height,
        video_parameters.frame_rate,
        &header
    );

    header.header.full_message_len = bswap_32(full_message_len);

    return usb_active_accessory_write_with_padding_512(
        &header, sizeof(message_video_frame_v2_t),
        buffer, len
    );
}

static gboolean message_processor_v2_handle_bin_msg(message_processor_v2_message_t *message) {

    if (message->header->msg_type == MESSAGE_COMMON_HEADER_V2_MSG_TYPE_TOUCH) {
        return message_processor_v2_handle_touch(message->payload, message->payload_len);
    }

    return TRUE;
}

static gboolean message_processor_v2_handle_json_msg(const char* json, size_t len) {
    message_cmd_response_v2_t cmd_response = { 0 };

    if (message_cmd_response_v2_init(json, len, &cmd_response) < 0) {
        // Unknown or broken JSON is not a reason to drop the link
        return TRUE;
    }

    if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_HEARTBEAT) == 0) {
        return TRUE;
    }

    LOG_I(message_processor_v2_tag, "Received command: %s", cmd_response.cmd);

    if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_VIDEO_SUP_REQ) == 0) {
        return message_processor_v2_answer_video_support_type(json, len);

    } else if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_KEY_FRAME_REQ) == 0) {
        messaging_service_send_key_frame_req();

    } else if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_VIDEO_CTRL) == 0) {
        return message_processor_v2_handle_video_ctrl(json, len);

    } else if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_VIDEO_ARGS) == 0) {
        // The head unit announces the preferred stream parameters. Sources already
        // encode at the head unit resolution, so it is only logged for diagnostics.
        LOG_I(message_processor_v2_tag, "VIDEO_ARGS: %.*s", (int)len, json);

    } else if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_DISCONNECT_REQ) == 0) {
        LOG_I(message_processor_v2_tag, "Head unit requested disconnect");

        video_receiver_deactivate();
    }

    return TRUE;
}

static gboolean message_processor_v2_handle_video_ctrl(const char* json, size_t len) {
    message_video_ctrl_parameters_v2_t parameters = { 0 };

    if (message_video_ctrl_parameters_v2_init(json, len, &parameters) < 0) {
        LOG_E(message_processor_v2_tag, "Error while parsing video ctrl");

        return TRUE;
    }

    LOG_I(message_processor_v2_tag, "Received play status: %d", parameters.play_status);

    if (parameters.play_status == 1) {
        video_receiver_activate();

        // The decoder on the head unit is restarted, it needs an IDR frame with SPS/PPS
        messaging_service_send_key_frame_req();
    } else {
        video_receiver_deactivate();
    }

    return TRUE;
}

static gboolean message_processor_v2_answer_video_support_type(const char* json, size_t len) {
    uint8_t *message_buffer = NULL;
    size_t message_len = 0;
    gboolean status;
    message_video_support_parameters_v2_t parameters = { 0 };

    if (message_video_support_req_parameters_v2_init(json, len, &parameters) < 0) {
        LOG_E(message_processor_v2_tag, "Error while parsing video support type");

        return TRUE;
    }

    LOG_I(message_processor_v2_tag, "Received video support req. Format: %d", parameters.video_format);

    parameters.video_support = parameters.video_format == MESSAGE_VIDEO_SUPPORT_FORMAT_H264 ? 1 : 0;

    if (message_video_support_rsp_parameters_v2_init(&parameters, &message_buffer, &message_len) < 0) {
        return FALSE;
    }

    status = message_processor_v2_send_json(message_buffer, message_len);

    free(message_buffer);

    return status;
}

static gboolean message_processor_v2_send_json(uint8_t *message_buffer, size_t message_len) {
    return usb_active_accessory_write_with_padding_512(NULL, 0, (void*)message_buffer, message_len) > 0;
}

static gboolean message_processor_v2_handle_touch(const uint8_t* payload, size_t len) {
    message_touches_v2_t touches;
    messaging_service_input_event_t input_event = { 0 };
    const message_touch_finger_v2_t *first_touch = NULL;
    size_t pressed_fingers = 0;

    if (message_touch_v2_parse(payload, len, &touches) < 0) {
        LOG_W(message_processor_v2_tag, "Malformed touch event skipped");

        return TRUE;
    }

    for (size_t i = 0; i < touches.fingers_count; i++) {
        if (touches.fingers[i].finger_action != MESSAGE_TOUCH_FINGER_V2_ACTION_UP) {
            pressed_fingers++;
        }
    }

    // Multi-finger gesture switches between the launcher and the projected phone

    if (pressed_fingers >= MESSAGE_PROCESSOR_V2_SWITCH_GESTURE_FINGERS) {
        if (message_processor_v2_gesture_fired == FALSE) {
            message_processor_v2_gesture_fired = TRUE;

            LOG_I(message_processor_v2_tag, "Source switch gesture");

            // Release the touch in the current source before switching away

            input_event.action = MESSAGING_SERVICE_INPUT_EVENT_UP;
            input_event.x = (int16_t)touches.fingers[0].x;
            input_event.y = (int16_t)touches.fingers[0].y;

            messaging_service_send_input_event(input_event);
            video_receiver_switch_to_previous();
        }

        return TRUE;
    }

    if (message_processor_v2_gesture_fired) {
        // Ignore the rest of the gesture until every finger is released
        if (pressed_fingers == 0) {
            message_processor_v2_gesture_fired = FALSE;
        }

        return TRUE;
    }

    // Like the phone application, only the primary finger drives the pointer

    for (size_t i = 0; i < touches.fingers_count; i++) {
        if (touches.fingers[i].finger_id == 0) {
            first_touch = &touches.fingers[i];

            break;
        }
    }

    if (first_touch == NULL) {
        first_touch = &touches.fingers[0];
    }

    switch (first_touch->finger_action)
    {
    case MESSAGE_TOUCH_FINGER_V2_ACTION_DOWN:
        input_event.action = MESSAGING_SERVICE_INPUT_EVENT_DOWN;
        break;

    case MESSAGE_TOUCH_FINGER_V2_ACTION_UP:
        input_event.action = MESSAGING_SERVICE_INPUT_EVENT_UP;
        break;

    case MESSAGE_TOUCH_FINGER_V2_ACTION_MOVE:
        input_event.action = MESSAGING_SERVICE_INPUT_EVENT_MOVE;
        break;

    default:
        return TRUE;
    }

    if (!(first_touch->x >= 0.0f && first_touch->x < 32767.0f && first_touch->y >= 0.0f && first_touch->y < 32767.0f)) {
        return TRUE;
    }

    input_event.x = (int16_t)first_touch->x;
    input_event.y = (int16_t)first_touch->y;

    messaging_service_send_input_event(input_event);

    return TRUE;
}
