#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <ctype.h>
#include <byteswap.h>
#include "usb_accessory/message_processor/message_processor.h"
#include "services/video_receiver/video_receiver.h"
#include "services/messaging_service/messaging_service.h"
#include "usb_accessory/usb_accessory_worker.h"
#include "usb_accessory/usb_active_accessory.h"
#include "messages/v1/messages.h"
#include "macros/data_types.h"
#include "macros/comand_types.h"
#include "logging/logging.h"
#include "hu_message.h"

#pragma mark - Private definitions

#define MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE                         (512)
#define MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE_LOG2                    (9)
#define MESSAGE_PROCESSOR_V1_DEFAULT_SCREEN_MESSAGE_BUFFER_SIZE         (65535)
#define MESSAGE_PROCESSOR_V1_PROBE_CHECK_INTERVAL_US                    (10000) // 10 ms
#define MESSAGE_PROCESSOR_V1_DEFAULT_FRAME_INTERVAL_SEC                 (1)

#pragma mark - Private methods definitions

static gboolean    message_processor_v1_probe(uint8_t * buffer, size_t len);
static gboolean    message_processor_v1_run(void);
static gboolean    message_processor_v1_write_hb(void);
static int         message_processor_v1_write_h264(message_processor_video_params_t video_parameters, void * buffer, size_t len);

static gboolean    message_processor_v1_reply_phone_ready(void);
static gboolean    message_processor_v1_reply_mirror_support(void);
static gboolean    message_processor_v1_reply_land_mode(land_mode_t* input_land_mode);
static gboolean    message_processor_v1_reply_start_seq(version_t* input_version);
static gboolean    message_processor_v1_reply_phone_init_ok(void);
static gboolean    message_processor_v1_reply_need_upgrade(void);
static gboolean    message_processor_v1_reply_bt_auto_connected(void);
 
static gboolean    message_processor_v1_handle_cmd(common_header_t * header, uint8_t* buffer, size_t len);
static gboolean    message_processor_v1_handle_app_msg(common_header_t * header, uint8_t* buffer, size_t len);
static gboolean    message_processor_v1_handle_screen(common_header_t * header, uint8_t* buffer, size_t len);
static gboolean    message_processor_v1_handle_control(common_header_t * header, uint8_t* buffer, size_t len);
static gboolean    message_processor_v1_handle_speech(common_header_t * header, uint8_t* buffer, size_t len);
static gboolean    message_processor_v1_send_app_msg(const hu_message_t* msg);

#pragma mark - Private propoerties

const gchar* message_processor_v1_tag = "MessageProcessorV1";

const message_processor_t message_provessor_v1 = {
    .probe = message_processor_v1_probe,
    .run = message_processor_v1_run,
    .write_hb = message_processor_v1_write_hb,
    .write_h264 = message_processor_v1_write_h264
};

#pragma mark - Internal methods implementaion

const message_processor_t* message_processor_v1_get(void) {
    return &message_provessor_v1;
}

#pragma mark - Private methods implementan

static gboolean message_processor_v1_probe(uint8_t * buffer, size_t len) {
    if (len < sizeof(version_t) || buffer == NULL) {
        return FALSE;
    }

    version_t * version_cmd = (version_t*)buffer;

    uint32_t action = bswap_32(version_cmd->common_header.action);
    uint32_t data_type = bswap_32(version_cmd->common_header.data_type);
    uint32_t cmd = bswap_32(version_cmd->common_command_header.cmd);

    // The first message from the head unit should contail structure of version cmd

    if (action != 1 || data_type != MESSAGE_DATA_TYPE_CMD || cmd != MESSAGE_CMD_VERSION) {
        LOG_I(message_processor_v1_tag, "Probe failed, unrecognized packet format");

        return FALSE;
    }

    LOG_I(message_processor_v1_tag, "Probe success: Legacy format");

    // Tell the car that our display is the same size as the head unit

    return message_processor_v1_reply_start_seq(version_cmd);
}

static gboolean message_processor_v1_write_hb(void) {
    uint8_t * message = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    if (message == NULL) {
        LOG_E(message_processor_v1_tag, "Can`t write heartbeat. Out of memory!");

        return FALSE;
    }

    heart_beat_v1_init((heart_beat_v1_t*)message);

    if (usb_active_accessory_write(message, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE) <= 0) {
        free(message);

        return FALSE;
    }

    free(message);

    return TRUE;
}

static int message_processor_v1_write_h264(message_processor_video_params_t video_parameters, void * buffer, size_t len) {
    static uint8_t* message_buffer = NULL;
    static size_t message_buffer_size = 0;

    if (len == 0) {
		return -EINVAL;
	}

	uint32_t header_size = MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE;
	uint32_t chunked_size = (len >> MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE_LOG2) << MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE_LOG2;

	if (chunked_size < len) {
		chunked_size += MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE;
	}

	uint32_t total_size = header_size + chunked_size;

    if (message_buffer == NULL) {
        message_buffer = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_SCREEN_MESSAGE_BUFFER_SIZE);

        if (message_buffer == NULL) {
            return -ENOMEM;
        }

        message_buffer_size = MESSAGE_PROCESSOR_V1_DEFAULT_SCREEN_MESSAGE_BUFFER_SIZE;
    }

	if (total_size > message_buffer_size) {
		uint8_t* resized_buffer = (uint8_t*)realloc(message_buffer, total_size);

		if (resized_buffer == NULL) {
			return -ENOMEM;
		}

		message_buffer = resized_buffer;
		message_buffer_size = total_size;
	}

	// Cleanup buffer

	memset((void*)message_buffer, 0, total_size);

	// Map buffer to screen header

	screen_header_t * screen_header = (screen_header_t*)message_buffer;

	// Initialize response header

    qd_screen_header_h264_response_init(
        screen_header,
        video_parameters.width,
        video_parameters.height,
        video_parameters.frame_rate,
        MESSAGE_PROCESSOR_V1_DEFAULT_FRAME_INTERVAL_SEC
    );

	// Fill data len

	screen_header->common_header.total_size = bswap_32(total_size);
	screen_header->capture_data_len = bswap_32(len);

	// Copy video data

	memcpy((void*)message_buffer + header_size, buffer, len);

	// Send buffer to QD sink

	return usb_active_accessory_write((void*)message_buffer, total_size);
}

static gboolean message_processor_v1_run(void) {
    uint8_t* read_buffer = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);
    gboolean status = TRUE;

    if (!read_buffer) {
        LOG_E(message_processor_v1_tag, "Can`t run message processor: Out of memory!");

        return FALSE;
    }

    memset(read_buffer, 0, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    do {
        ssize_t readed = usb_active_accessory_read(
            read_buffer,
            MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE
        );
    
        if (readed < 0) {
            LOG_E(message_processor_v1_tag, "Device disconnected!");

            status = FALSE;

            break;
        }
    
        common_header_t * header = (common_header_t*)read_buffer;
    
        if (memcmp(header->binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN) != 0) {
            LOG_E(message_processor_v1_tag, "Broken sequence!");

            status = FALSE;

            break;
        }
    
        uint32_t data_type = bswap_32(header->data_type);
        uint32_t action = bswap_32(header->action);
    
        if (data_type == MESSAGE_DATA_TYPE_CMD && action == 1) {
            status = message_processor_v1_handle_cmd(
                header, read_buffer,
                MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE
            );

        } else if (data_type == MESSAGE_DATA_TYPE_SCREEN_CAPTURE) {
            status = message_processor_v1_handle_screen(
                header, read_buffer,
                MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE
            );

        } else if (data_type == MESSAGE_DATA_TYPE_HU_MSG && action == 1) {
            status = message_processor_v1_handle_app_msg(
                header, read_buffer,
                MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE
            );

        } else if (data_type == MESSAGE_DATA_TYPE_IN_APP_CONTROL) {
            status = message_processor_v1_handle_control(
                header, read_buffer,
                MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE
            );

        } else if (data_type == MESSAGE_DATA_TYPE_SPEECH_STATUS) {
            status = message_processor_v1_handle_speech(
                header, read_buffer,
                MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE
            );

        } else {
            status = TRUE;

            LOG_W(message_processor_v1_tag, "Unprocessed data_type: %d, action %d", data_type, action);
        }
    } while (0);

    free(read_buffer);

    return status;
}

static gboolean message_processor_v1_handle_app_msg(common_header_t * header, uint8_t* buffer, size_t len) {
    hu_message_t * request_message = NULL;
    hu_message_t * response_message = NULL;
    gboolean status = TRUE;
    uint32_t total_len = bswap_32(header->total_size);
    uint32_t payload_len = total_len - len;
    uint8_t* payload = NULL;

    if (total_len == 0 || payload_len == 0) {
        return FALSE;
    }

    payload = (uint8_t*)malloc(payload_len);
    
    if (!payload) {
        return FALSE;
    }
    
    if ((payload_len > 0) && (usb_active_accessory_read(payload, payload_len) <= 0)) {
        free(payload);

        return FALSE;
    }
    
    request_message = hu_message_decode(payload, payload_len);

    free(payload);

    if (!request_message) {
        return FALSE;
    }

    LOG_I(message_processor_v1_tag, "Receive HU: %s", request_message->logic_id);

    if (strcmp(request_message->logic_id, "HUINFO") == 0) {
        status = message_processor_v1_reply_phone_init_ok() && message_processor_v1_reply_need_upgrade();

    } else if (strcmp(request_message->logic_id, "BT_AUTO_CONNECTED") == 0) {
        status = message_processor_v1_reply_bt_auto_connected();
    }

    return status;
}

static gboolean message_processor_v1_handle_cmd(common_header_t * header, uint8_t* buffer, size_t len) {
    common_comand_header_t * comand_header = NULL;
    gboolean status = TRUE;
    uint32_t cmd = 0;

    if (header == NULL || buffer == NULL || len < sizeof(common_header_t) + sizeof(common_comand_header_t)) {
        return FALSE;
    }
    
    comand_header = (common_comand_header_t *)(buffer + sizeof(common_header_t));
    cmd = bswap_32(comand_header->cmd);
    
    if (cmd == MESSAGE_CMD_LAND_MODE && len >= sizeof(land_mode_t)) {
        status = message_processor_v1_reply_land_mode(
            (land_mode_t*)buffer
        );

    } else if (cmd == MESSAGE_CMD_MIRROR_SUPPORT) {
        status = message_processor_v1_reply_phone_ready() && 
            message_processor_v1_reply_mirror_support();

    } else if (cmd == MESSAGE_CMD_PLAY_STATUS) {
        LOG_I(message_processor_v1_tag, "Receive play status");

        video_receiver_activate();
        messaging_service_send_key_frame_req();

    }  else if (cmd == MESSAGE_CMD_KEY_FRAME_REQ) {
        messaging_service_send_key_frame_req();

    } else if (cmd == MESSAGE_CMD_HEARTBEAT){
        LOG_I(message_processor_v1_tag, "Receive heardbeat cmd...");

    } else {
        LOG_W(message_processor_v1_tag, "Unprocessed cmd: %d", cmd);
    }

    return status;
}

static gboolean message_processor_v1_reply_mirror_support(void) {
    uint8_t * response = NULL;
    
    response = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);
        
    if (!response) {
        return FALSE;
    }

    memset(response, 0, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);
    mirror_support_response_init((mirror_support_t*)response);

    if (usb_active_accessory_write((void*)response, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE) <= 0) {
        free(response);

        return FALSE;
    }

    free(response);

    return TRUE;
}

static gboolean message_processor_v1_reply_phone_ready(void) {
    hu_message_t * response_message = NULL;
    
    response_message = hu_message_init(
        "QDRIVE_ASSISTANT",
        "phoneready",
        0
    );

    if (response_message == NULL) {
        return FALSE;
    }

    if (message_processor_v1_send_app_msg(response_message) == FALSE) {
        hu_message_free(response_message);

        return FALSE;
    }

    hu_message_free(response_message);

    return TRUE;
}

static gboolean message_processor_v1_reply_phone_init_ok(void) {
    hu_message_t * response_message = NULL;
    
    response_message = hu_message_init(
        "QDRIVE_ASSISTANT",
        "phoneinitok",
        0
    );

    if (response_message == NULL) {
        return FALSE;
    }

    if (message_processor_v1_send_app_msg(response_message) == FALSE) {
        hu_message_free(response_message);

        return FALSE;
    }

    hu_message_free(response_message);

    return TRUE;
}

static gboolean message_processor_v1_reply_need_upgrade(void) {
    hu_message_t * response_message = NULL;
    
    response_message = hu_message_init(
        "QDRIVE_ASSISTANT",
        "NEEDUPGRADE",
        1,
        "{\"D\":0,\"T\":\"i\",\"V\":1}"
    );

    if (response_message == NULL) {
        return FALSE;
    }

    if (message_processor_v1_send_app_msg(response_message) == FALSE) {
        hu_message_free(response_message);

        return FALSE;
    }

    hu_message_free(response_message);

    return TRUE;
}

static gboolean message_processor_v1_reply_bt_auto_connected(void) {
    hu_message_t * response_message = NULL;
    
    response_message = hu_message_init(
        "QDRIVE_ASSISTANT",
        "BT_AUTO_CONNECTED",
        1,
        "{\"D\":1,\"T\":\"(is)\",\"V\":[{\"D\":0,\"T\":\"i\",\"V\":0},{\"D\":0,\"T\":\"s\",\"V\":\"\"}]}"
    );

    if (response_message == NULL) {
        return FALSE;
    }

    if (message_processor_v1_send_app_msg(response_message) == FALSE) {
        hu_message_free(response_message);

        return FALSE;
    }

    hu_message_free(response_message);

    return TRUE;
}

static gboolean message_processor_v1_reply_land_mode(land_mode_t * input_land_mode) {
    uint8_t* response = NULL;
    land_mode_t * response_land_mode = NULL;

    if (input_land_mode == NULL) {
        return FALSE;
    }

    response = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    if (!response) {
        return FALSE;
    }

    memset(response, 0, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);
    
    response_land_mode = (land_mode_t *)response;
    land_mode_response_init(response_land_mode);

    response_land_mode->ret = input_land_mode->value;
    response_land_mode->is_value = bswap_32(2);

    if (usb_active_accessory_write(response, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE) <= 0) {
        free(response);

        return FALSE;
    }

    free(response);

    return TRUE;
}

static gboolean message_processor_v1_reply_start_seq(version_t* input_version) {
    uint8_t * response = NULL;
    upgrade_t * response_upgrade = NULL;
    version_t * response_version = NULL;
    int write_result = -EINVAL;

    response = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    if (response == NULL) {
        return FALSE;
    }

    response_version = (version_t *)response;
    version_response_init(response_version);

    response_version->phone_width = input_version->car_width;
    response_version->phone_height = input_version->car_height;
    response_version->out_app_width = input_version->car_width;
    response_version->out_app_height = input_version->car_height;

    LOG_I(message_processor_v1_tag, "Got screen parameters: w: %d, h: %d", bswap_32(input_version->car_width), bswap_32(input_version->car_height));

    if (usb_active_accessory_write((void*)response, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE) <= 0) {
        free(response);

        return FALSE;
    }

    memset((void*)response, 0, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    LOG_I(message_processor_v1_tag, "Writing upgrade response ...");

    response_upgrade = (upgrade_t *)response;
    upgrade_response_init(response_upgrade);

    if (usb_active_accessory_write((void*)response, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE) <= 0) {
        free(response);

        return FALSE;
    }

    free(response);

    return TRUE;
}

static gboolean message_processor_v1_handle_screen(common_header_t * header, uint8_t* buffer, size_t len) {
    uint8_t* response = NULL;
    uint32_t action = bswap_32(header->action);

    // Unknown condition from original application
    if (action == 18) {
        return TRUE;
    }

    LOG_I(message_processor_v1_tag, "Receive screen params");

    response = (uint8_t*)malloc(MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    if (!response) {
        return FALSE;
    }

    memset(response, 0, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);
    
    speech_response_init((speech_t*)response);

    int write_status = usb_active_accessory_write(response, MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE);

    free(response);

    return write_status > 0 ? TRUE : FALSE;
}

static gboolean message_processor_v1_handle_control(common_header_t * header, uint8_t* buffer, size_t len) {
    input_control_t* input_control = (input_control_t*)buffer;

    if (input_control == NULL || len < sizeof(input_control_t)) {
        return FALSE;
    }

    // Handle touch screen data here

    messaging_service_input_event_t input_event = {
        .action = bswap_16(input_control->event_type),
        .x = bswap_16(input_control->event_value0),
        .y = bswap_16(input_control->event_value1)
    };

    messaging_service_send_input_event(input_event);
    
    return TRUE;
}

static gboolean message_processor_v1_handle_speech(common_header_t * header, uint8_t* buffer, size_t len) {
    uint32_t full_packet_len = ntohl(header->total_size);
    uint8_t* response = (uint8_t*)malloc(full_packet_len);
    
    if (!response) {
        return FALSE;
    }
    
    memcpy(response, buffer, len);
    
    if (usb_active_accessory_read(response + len, full_packet_len - len) <= 0) {
        free(response);
        
        return FALSE;
    }
    
    speech_t * speech = (speech_t*)response;
    
    (void)speech;
    
    free(response);

    return TRUE;
}

static gboolean message_processor_v1_send_app_msg(const hu_message_t* msg) {
    uint32_t encoded_hu_message_len = 0;
    uint8_t* encoded_hu_message = hu_message_encode(
        msg,
        0,
        &encoded_hu_message_len
    );

    if (!encoded_hu_message) {
        return FALSE;
    }

    uint32_t encoded_hu_message_chunked_len = (encoded_hu_message_len / MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE) * MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE;

    if (encoded_hu_message_chunked_len < encoded_hu_message_len) {
        encoded_hu_message_chunked_len += MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE;
    }

    uint32_t total_size = MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE + encoded_hu_message_chunked_len;

    uint8_t* response = (uint8_t*)malloc(total_size);

    if (!response) {
        free(encoded_hu_message);

        return FALSE;
    }

    memset(response, 0, total_size);

    app_message_t* app_message = (app_message_t*)response;

    app_message_response_init(app_message);

    app_message->common_header.total_size = ntohl(total_size);
    app_message->data_size = ntohl(encoded_hu_message_len);

    for (int i = 0; i < 32; i++) {
        app_message->common_header.mark[i] = i + 32;
    }

    memcpy(
        response + MESSAGE_PROCESSOR_V1_DEFAULT_CHUNK_SIZE,
        encoded_hu_message,
        encoded_hu_message_len
    );

    int write_status = usb_active_accessory_write(response, total_size);

    free(response);
    free(encoded_hu_message);

    return (write_status > 0) ? TRUE : FALSE;
}
