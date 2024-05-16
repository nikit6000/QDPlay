
#include <glib.h>
#include <string.h>
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

#pragma mark - Private methods definition

static gboolean    message_processor_v2_probe(uint8_t * buffer, size_t len);
static gboolean    message_processor_v2_run(void);
static gboolean    message_processor_v2_write_hb(void);
static int         message_processor_v2_write_h264(message_processor_video_params_t video_parameters, void * buffer, size_t len);

static gboolean    message_processor_v2_handle_bin_msg(message_common_header_v2_t* header, uint8_t* message, size_t len);
static gboolean    message_processor_v2_handle_json_msg(const char* json, size_t len);
static gboolean    message_processor_v2_handle_video_ctrl(const char* json, size_t len);
static gboolean    message_processor_v2_answer_video_support_type(const char* json, size_t len);
static gboolean    message_processor_v2_send_phone_info(uint16_t width, uint16_t height);
static gboolean    message_processor_v2_send_update_status_info(void);
static gboolean    message_processor_v2_handle_touch(uint8_t* message, size_t len);

#pragma mark - Private properties

const gchar* message_processor_v2_tag = "MessageProcessorV2";

const message_processor_t message_processor_v2 = {
    .probe = message_processor_v2_probe,
    .run = message_processor_v2_run,
    .write_hb = message_processor_v2_write_hb,
    .write_h264 = message_processor_v2_write_h264
};

#pragma mark - Internal methods

const message_processor_t* message_processor_v2_get(void) {
   return &message_processor_v2;
}

#pragma mark - Private methods

static gboolean message_processor_v2_probe(uint8_t * buffer, size_t len) {
    message_common_header_v2_t *message_header = NULL;
    message_cmd_response_v2_t cmd_response = { 0 };
    message_car_info_parameters_v2_t car_info = { 0 };
    uint8_t * full_message_buffer = NULL;
    uint32_t full_message_len = 0;
    size_t full_chunked_message_len = 0;

    if (len != MESSAGE_PROCESSOR_V2_CHUNK_SIZE || buffer == NULL) {
        return FALSE;
    }

    message_header = (message_common_header_v2_t*)buffer;

    if (strncmp(message_header->mark, MESSAGE_COMMON_HEADER_V2_MARK, sizeof(message_header->mark)) != 0) {
        return FALSE;
    }

    if (message_header->source != MESSAGE_COMMON_HEADER_V2_SOURCE_CAR) {
        return FALSE;
    }

    if (message_header->payload_format != MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON) {
        return FALSE;
    }

    LOG_I(message_processor_v2_tag, "V2 format detected!");

    full_message_len = bswap_32(message_header->full_message_len);

    full_chunked_message_len = (full_message_len >> MESSAGE_PROCESSOR_V2_CHUNK_SIZE_LOG2) << MESSAGE_PROCESSOR_V2_CHUNK_SIZE_LOG2;

	if (full_chunked_message_len < full_message_len) {
		full_chunked_message_len += MESSAGE_PROCESSOR_V2_CHUNK_SIZE;
	}

    LOG_I(message_processor_v2_tag, "Readed size: %d, full chunked size: %d!", len, full_chunked_message_len);

    if (full_chunked_message_len > len) {
        LOG_I(message_processor_v2_tag, "Allocating new buffer ...");

        full_message_buffer = (uint8_t*)malloc(full_chunked_message_len);

        if (full_message_buffer == NULL) {
            return FALSE;
        }

        memcpy(full_message_buffer, buffer, len);

        if (
            usb_active_accessory_read(
                full_message_buffer + len,
                full_chunked_message_len - len
            ) <= 0
        ) {
            free(full_message_buffer);

            return FALSE;
        }

        LOG_I(message_processor_v2_tag, "Rebinding buffer ...");

        buffer = full_message_buffer;
        len = full_chunked_message_len;
        message_header = (message_common_header_v2_t*)buffer;

        LOG_I(message_processor_v2_tag, "Done!");
    }

    if (
        message_cmd_response_v2_init(
            buffer + sizeof(message_common_header_v2_t),
            full_message_len - sizeof(message_common_header_v2_t),
            &cmd_response
        ) < 0
    ) {
        if (full_message_buffer) {
            free(full_message_buffer);
        }

        return FALSE;
    }

    if (
        strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_CAR_INFO) != 0
    ) {
        if (full_message_buffer) {
            free(full_message_buffer);
        }

        return FALSE;
    }

    if (
        message_car_info_parameters_v2_init(
            buffer + sizeof(message_common_header_v2_t),
            full_message_len - sizeof(message_common_header_v2_t),
            &car_info
        ) < 0
    ) {
        if (full_message_buffer) {
            free(full_message_buffer);
        }

        return FALSE;
    }

    LOG_I(message_processor_v2_tag, "Got car info w: %d, h: %d", car_info.width, car_info.height);

    if (full_message_buffer) {
        free(full_message_buffer);
    }

    LOG_I(message_processor_v2_tag, "Sending phone info ...");

    if (message_processor_v2_send_phone_info(car_info.width, car_info.height) == FALSE) {
        return FALSE;
    }

    return message_processor_v2_send_update_status_info();
}

static gboolean message_processor_v2_run(void) {
    uint8_t *message_buffer = (uint8_t*)malloc(MESSAGE_PROCESSOR_V2_CHUNK_SIZE);
    size_t message_buffer_size = 0;
    message_common_header_v2_t *header = NULL;
    uint32_t full_message_len = 0;
    int received_data_len = 0;
    gboolean status = TRUE;

    if (message_buffer == NULL) {
        return FALSE;
    }

    message_buffer_size = MESSAGE_PROCESSOR_V2_CHUNK_SIZE;

    do {
        received_data_len = usb_active_accessory_read(message_buffer, MESSAGE_PROCESSOR_V2_CHUNK_SIZE);

        if (received_data_len != MESSAGE_PROCESSOR_V2_CHUNK_SIZE) {
            status = FALSE;

            break;
        }

        header = (message_common_header_v2_t *)message_buffer;
        full_message_len = bswap_32(header->full_message_len);

        if (full_message_len > received_data_len) {
            uint32_t full_chunked_message_len = (full_message_len >> MESSAGE_PROCESSOR_V2_CHUNK_SIZE_LOG2) << MESSAGE_PROCESSOR_V2_CHUNK_SIZE_LOG2;

            if (full_chunked_message_len < full_message_len) {
                full_chunked_message_len += MESSAGE_PROCESSOR_V2_CHUNK_SIZE;
            }

            uint8_t *resized_buffer = (uint8_t*)realloc(message_buffer, full_chunked_message_len);

            if (resized_buffer == NULL) {
                status = FALSE;

                break;
            }

            message_buffer = resized_buffer;
            message_buffer_size = full_chunked_message_len;
            header = (message_common_header_v2_t *)message_buffer;

            received_data_len = usb_active_accessory_read(
                message_buffer + received_data_len, 
                full_chunked_message_len - received_data_len
            );

            if (received_data_len != (full_chunked_message_len - received_data_len)) {
                status = FALSE;

                break;
            }
        }

        if (header->source != MESSAGE_COMMON_HEADER_V2_SOURCE_CAR) {
            break;
        }

        if (header->payload_format == MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON) {
            status = message_processor_v2_handle_json_msg(
                (const char *)(message_buffer + sizeof(message_common_header_v2_t)),
                full_message_len - sizeof(message_common_header_v2_t)
            );

        } else if (header->payload_format == MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_BIN) {
            status = message_processor_v2_handle_bin_msg(
                header,
                message_buffer,
                message_buffer_size
            );
        }

    } while (0);

    free(message_buffer);

    return status;
}

static gboolean message_processor_v2_write_hb(void) {
    uint8_t *message = (uint8_t*)malloc(MESSAGE_PROCESSOR_V2_CHUNK_SIZE);

    if (message == NULL) {
        return FALSE;
    }

    if (message_heart_beat_v2_init(message, MESSAGE_PROCESSOR_V2_CHUNK_SIZE) < 0) {
        free(message);

        return FALSE;
    }

    if (usb_active_accessory_write(message, MESSAGE_PROCESSOR_V2_CHUNK_SIZE) <= 0) {
        free(message);

        return FALSE;
    }

    free(message);

    return TRUE;
}

static int message_processor_v2_write_h264(message_processor_video_params_t video_parameters, void * buffer, size_t len) {
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

static gboolean message_processor_v2_handle_bin_msg(message_common_header_v2_t* header, uint8_t* message, size_t len) {

    if (header->msg_type == MESSAGE_COMMON_HEADER_V2_MSG_TYPE_TOUCH) {
        return message_processor_v2_handle_touch(message, len);
    }

    return TRUE;
}

static gboolean message_processor_v2_handle_json_msg(const char* json, size_t len) {
    message_cmd_response_v2_t cmd_response = { 0 };

    if (
        message_cmd_response_v2_init(
            json,
            len,
            &cmd_response
        ) < 0
    ) {
        // Just skip

        return TRUE;
    }

    if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_VIDEO_SUP_REQ) == 0) {
        return message_processor_v2_answer_video_support_type(json, len);

    } else if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_KEY_FRAME_REQ) == 0) {
        messaging_service_send_key_frame_req();

    } else if (strcmp(cmd_response.cmd, MESSAGE_V2_JSON_CMD_VIDEO_CTRL) == 0) {
        return message_processor_v2_handle_video_ctrl(json, len);
    }

    return TRUE;
}

static gboolean message_processor_v2_handle_video_ctrl(const char* json, size_t len) {
    message_video_ctrl_parameters_v2_t parameters = { 0 };

    if (
        message_video_ctrl_parameters_v2_init(
            json,
            len,
            &parameters
        ) < 0
    ) {
        LOG_E(message_processor_v2_tag, "Error while parsing video ctrl");

        return FALSE;
    }

    LOG_I(message_processor_v2_tag, "Received play status: %d", parameters.play_status);

    if (parameters.play_status) {
        video_receiver_activate();
    } else {
        video_receiver_deactivate();
    }

    return TRUE;
}

static gboolean message_processor_v2_answer_video_support_type(const char* json, size_t len) {
    uint8_t *message_buffer = NULL;
    size_t message_len = 0;
    message_video_support_parameters_v2_t parameters = { 0 };

    if (
        message_video_support_req_parameters_v2_init(
            json,
            len,
            &parameters
        ) < 0
    ) {
        LOG_E(message_processor_v2_tag, "Error while parsing video support type");

        return FALSE;
    }

    LOG_E(message_processor_v2_tag, "Received video support req. Format: %d", parameters.video_format);

    if (parameters.video_format == MESSAGE_VIDEO_SUPPORT_FORMAT_H264) {
        parameters.video_support = 1;
    } else {
        parameters.video_support = 0;
    }

    if (
        message_video_support_rsp_parameters_v2_init(
            &parameters,
            &message_buffer,
            &message_len
        ) < 0
    ) {
        return FALSE;
    }

    if (
        usb_active_accessory_write_with_padding_512(
            NULL, 0,
            (void*)message_buffer, message_len
        ) < 0
    ) {
        free(message_buffer);

        return FALSE;
    }

    free(message_buffer);

    return TRUE;
}

static gboolean message_processor_v2_send_phone_info(uint16_t width, uint16_t height) {
    uint8_t * message_buffer = NULL;
    size_t message_len = 0;

    if (message_phone_info_v2_init(width, height, &message_buffer, &message_len) < 0) {
        return FALSE;
    }

    if (
        usb_active_accessory_write_with_padding_512(
            NULL, 0,
            (void*)message_buffer, message_len
        ) < 0
    ) {
        free(message_buffer);

        return FALSE;
    }

    free(message_buffer);

    return TRUE;
}

static gboolean message_processor_v2_send_update_status_info(void) {
    uint8_t * message_buffer = NULL;
    size_t message_len = 0;

    if (message_update_satus_v2_init(5, &message_buffer, &message_len) < 0) {
        return FALSE;
    }

    if (
        usb_active_accessory_write_with_padding_512(
            NULL, 0,
            (void*)message_buffer, message_len
        ) < 0
    ) {
        free(message_buffer);

        return FALSE;
    }

    free(message_buffer);

    return TRUE;
}

static gboolean message_processor_v2_handle_touch(uint8_t* message, size_t len) {
    message_touches_v2_t *touches = message_touch_v2_init((void*)message, len);
    messaging_service_input_event_t input_event = { 0 };

    if (touches == NULL) {
        return TRUE;
    }

    if (touches->fingers_count == 0) {
        return TRUE;
    }

    message_touch_finger_v2_t *first_touch = touches->fingers[0];

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
        message_touch_v2_free(touches);
        
        return TRUE;
    }

    input_event.x = first_touch->x;
    input_event.y = first_touch->y;

    messaging_service_send_input_event(input_event);
    message_touch_v2_free(touches);

    return TRUE;
}