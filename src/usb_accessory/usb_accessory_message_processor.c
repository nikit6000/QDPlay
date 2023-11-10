#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include "usb_accessory/usb_accessory_message_processor.h"
#include "messages/messages.h"
#include "macros/data_types.h"
#include "macros/comand_types.h"
#include "hu_message.h"
#include <ctype.h>

#pragma mark - Private definitions

#define USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN            (512)
#define USB_ACCESSORY_MESSAGE_PROCESSOR_BINARY_PACKET_SIGN            "!BIN"

#pragma mark - Private methods definitions

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_cmd(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_screen(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_hu_msg(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_control(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_speech(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_send_app_msg(int fd, const hu_message_t* msg);

#pragma mark - Internal methods implementations

usb_accessory_msg_processor_status_t usb_accessory_message_processor_setup(int acccessory_fd) {
    uint8_t* buffer = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);

    if (!buffer) {
        return USB_ACCESSORY_MSG_ERR_NO_MEM;
    }

    app_status_t * app_status = (app_status_t*)buffer;

    memset(buffer, 0, USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
    memcpy(app_status->common_header.binary_mark, USB_ACCESSORY_MESSAGE_PROCESSOR_BINARY_PACKET_SIGN, 4);
    
    app_status->common_header.data_type = ntohl(0);
    app_status->common_header.header_size = ntohl(512);
    app_status->common_header.total_size = ntohl(512);
    app_status->common_header.common_header_size = ntohl(64);
    app_status->common_header.request_header_size = ntohl(128);
    app_status->common_header.response_header_size = ntohl(128);
    app_status->common_header.action = ntohl(2);
    app_status->common_command_header.time_stamp = ntohl(0);
    app_status->common_command_header.cmd = ntohl(1);
    app_status->value = ntohl(1);
    app_status->android_version = ntohl(24);
    app_status->integrator_server = 2;

    for (int i = 0; i < 32; i++) {
        app_status->common_header.mark[i] = i + 32;
    }

    usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;

    while(write(acccessory_fd, buffer, USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN) < 0) {
        
        if (errno != 19) {
            status = USB_ACCESSORY_MSG_ERR_IO;
            
            break;
        }

        printf("Awaiting accessory ...\n");
        usleep(100000);
    }

    free(buffer);

    return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle(int acccessory_fd) {
    uint8_t* read_buffer = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
    
    if (!read_buffer) {
        return USB_ACCESSORY_MSG_ERR_NO_MEM;
    }
    
    if (acccessory_fd < 0) {
        free(read_buffer);
        
        return USB_ACCESSORY_MSG_ERR_BROKEN_FD;
    }
    
    ssize_t readed = read(
        acccessory_fd,
        read_buffer,
        USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
    );
    
    if (readed < 0) {
        free(read_buffer);
        
        return USB_ACCESSORY_MSG_ERR_IO;
    }
    
    common_header_t * header = (common_header_t*)read_buffer;
    
    if (memcmp(header->binary_mark, USB_ACCESSORY_MESSAGE_PROCESSOR_BINARY_PACKET_SIGN, 4) != 0) {
        free(read_buffer);
        
        return USB_ACCESSORY_MSG_ERR_IO;
    }
    
    usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
    uint32_t data_type = ntohl(header->data_type);
    uint32_t action = ntohl(header->action);
    
    if (data_type == MESSAGE_DATA_TYPE_CMD && action == 1) {
        status = usb_accessory_message_processor_handle_cmd(
            acccessory_fd,
            header, read_buffer,
            USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
        );
    } else if (data_type == MESSAGE_DATA_TYPE_SCREEN_CAPTURE) {
        status = usb_accessory_message_processor_handle_screen(
            acccessory_fd,
            header, read_buffer,
            USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
        );
    } else if (data_type == MESSAGE_DATA_TYPE_HU_MSG && action == 1) {
        status = usb_accessory_message_processor_handle_hu_msg(
            acccessory_fd,
            header, read_buffer,
            USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
        );
    } else if (data_type == MESSAGE_DATA_TYPE_IN_APP_CONTROL) {
        status = usb_accessory_message_processor_handle_control(
            acccessory_fd,
            header, read_buffer,
            USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
        );
    } else if (data_type == MESSAGE_DATA_TYPE_SPEECH_STATUS) {
        status = usb_accessory_message_processor_handle_speech(
            acccessory_fd,
            header, read_buffer,
            USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
        );
    }
    
    free(read_buffer);
    
    return status;
}

#pragma mark - Private methods implementations

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_cmd(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
    usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
    common_comand_header_t * comand_header = (common_comand_header_t *)(buffer + sizeof(common_header_t));
    
    uint32_t cmd = ntohl(comand_header->cmd);
    uint8_t* response = NULL;
    uint8_t is_phone_ready = 0;
    
    if (cmd == MESSAGE_CMD_VERSION) {
        response = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
        memcpy(response, buffer, len);
        
        version_t * version = (version_t *)response;
        
        version->out_app_width = version->car_width;
        version->out_app_height = version->car_height;
        version->phone_width = version->car_width;
        version->phone_width = version->car_height;
        version->ret = ntohl(24);
    } else if (cmd == MESSAGE_CMD_LAND_MODE) {
        response = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
        memcpy(response, buffer, len);
        
        land_mode_t * land_mode = (land_mode_t *)response;
        
        land_mode->ret = land_mode->value;
        land_mode->is_value = ntohl(2);
    } else if (cmd == MESSAGE_CMD_PLAY_STATUS) {
        printf("[MESSAGE_PROCESSOR] receive play status\n");
    } else if (cmd == MESSAGE_CMD_MIRROR_SUPPORT) {
        response = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
        memcpy(response, buffer, len);
        
        mirror_support_t * mirror_support = (mirror_support_t *)response;
        
        mirror_support->ret = ntohl(1);
        mirror_support->screen_capture_support = ntohl(0);

        is_phone_ready = 1;
    } else if (cmd == MESSAGE_CMD_RESEND_SPS) {
        printf("[MESSAGE_PROCESSOR] needs sps\n");
    }
    
    if (response) {
        
        if (write(fd, response, USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN) < 0) {
            status = USB_ACCESSORY_MSG_ERR_IO;
        }
        
        free(response);
    }

    if (is_phone_ready) {
        hu_message_t response_message;
        memset(&response_message, 0, sizeof(hu_message_t));
        
        response_message.flow_id = 0;
        response_message.app_id = "QDRIVE_ASSISTANT";
        response_message.logic_id = "phoneready";

        printf("Sending phoneready...\n");

        return usb_accessory_message_send_app_msg(fd, &response_message);
    }
    
    return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_screen(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
    usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
    
    return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_hu_msg(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
    uint32_t total_len = ntohl(header->total_size);
    uint32_t payload_len = total_len - len;
    uint8_t* payload = NULL;

    if (total_len == 0 || payload_len == 0) {
        return USB_ACCESSORY_MSG_OK;
    }

    payload = (uint8_t*)malloc(payload_len);
    
    if (!payload) {
        return USB_ACCESSORY_MSG_ERR_NO_MEM;
    }
    
    if ((payload_len > 0) && (read(fd, payload, payload_len) < 0)) {
        free(payload);

        return USB_ACCESSORY_MSG_ERR_IO;
    }
    
    hu_message_t * request_message = hu_message_decode(payload, payload_len);
    hu_message_t * response_message = NULL;

    if (request_message) {
        if (strcmp(request_message->logic_id, "HUINFO") == 0) {
            response_message = hu_message_init(
                "QDRIVE_ASSISTANT",
                "phoneinitok",
                0
            );

            printf("Sending phoneinitok...\n");
        } else if (strcmp(request_message->logic_id, "BT_AUTO_CONNECTED") == 0) {
            response_message = hu_message_init(
                "QDRIVE_ASSISTANT",
                "BT_AUTO_CONNECTED",
                1,
                "{\"D\":1,\"T\":\"(is)\",\"V\":[{\"D\":0,\"T\":\"i\",\"V\":0},{\"D\":0,\"T\":\"s\",\"V\":\"\"}]}"
            );

            printf("Sending BT_AUTO_CONNECTED...\n");
        } else {
            printf("Not processed: %s\n", request_message->logic_id);
        }

        hu_message_free(request_message);
    }

    free(payload);
    
    if (!response_message) {
        return USB_ACCESSORY_MSG_OK;
    }

    usb_accessory_msg_processor_status_t status = usb_accessory_message_send_app_msg(fd, response_message);
    
    free(response_message);
    
    return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_control(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
    input_control_t* input_control = (input_control_t*)buffer;
    int16_t x1 = (int16_t)ntohs(input_control->event_value0);
    int16_t y1 = (int16_t)ntohs(input_control->event_value1);
    
    switch ((int16_t)ntohs(input_control->event_type)) {
        case INT16_MIN:
            printf("Touch down: x1: %d, x1: %d;\n", x1, y1);
            break;
            
        case -32767:
            printf("Touch move: x1: %d, x1: %d;\n", x1, y1);
            break;
            
        default:
            printf("Touch up: x1: %d, x1: %d;\n", x1, y1);
            break;
    }
    
    return USB_ACCESSORY_MSG_OK;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_speech(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
    uint32_t full_packet_len = ntohl(header->total_size);
    uint8_t* response = (uint8_t*)malloc(full_packet_len);
    
    if (!response) {
        return USB_ACCESSORY_MSG_ERR_NO_MEM;
    }
    
    memcpy(response, buffer, len);
    
    if (read(fd, response + len, full_packet_len - len) < 0) {
        free(response);
        
        return USB_ACCESSORY_MSG_ERR_IO;
    }
    
    speech_t * speech = (speech_t*)response;
    
    (void)speech;
    
    free(response);

    return USB_ACCESSORY_MSG_OK;
}

usb_accessory_msg_processor_status_t usb_accessory_message_send_app_msg(int fd, const hu_message_t* msg) {
    uint32_t encoded_hu_message_len = 0;
    uint8_t* encoded_hu_message = hu_message_encode(
        msg,
        0,
        &encoded_hu_message_len
    );

    if (!encoded_hu_message) {
        return USB_ACCESSORY_MSG_OK;
    }

    uint32_t encoded_hu_message_chunked_len = (encoded_hu_message_len / USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN) * USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN;

    if (encoded_hu_message_chunked_len < encoded_hu_message_len) {
        encoded_hu_message_chunked_len += USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN;
    }

    uint32_t total_size = USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN + encoded_hu_message_chunked_len;

    uint8_t* response = (uint8_t*)malloc(total_size);

    if (!response) {
        free(encoded_hu_message);

        return USB_ACCESSORY_MSG_ERR_NO_MEM;
    }

    memset(response, 0, total_size);

    app_message_t* app_message = (app_message_t*)response;

    memcpy(app_message->common_header.binary_mark, USB_ACCESSORY_MESSAGE_PROCESSOR_BINARY_PACKET_SIGN, 4);
    app_message->common_header.data_type = ntohl(MESSAGE_DATA_TYPE_HU_MSG);
    app_message->common_header.header_size = ntohl(512);
    app_message->common_header.total_size = ntohl(total_size);
    app_message->common_header.common_header_size = ntohl(64);
    app_message->common_header.request_header_size = ntohl(64);
    app_message->common_header.response_header_size = ntohl(0);
    app_message->common_header.action = ntohl(2);
    app_message->data_size = ntohl(encoded_hu_message_len);

    for (int i = 0; i < 32; i++) {
        app_message->common_header.mark[i] = i + 32;
    }

    memcpy(
        response + USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN,
        encoded_hu_message,
        encoded_hu_message_len
    );

    int write_status = write(fd, response, total_size);

    free(response);
    free(encoded_hu_message);

    return (write_status < 0) ? USB_ACCESSORY_MSG_ERR_IO : USB_ACCESSORY_MSG_OK;
}
