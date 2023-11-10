#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include "usb_accessory/usb_accessory_message_processor.h"
#include "messages/messages.h"
#include "macros/data_types.h"
#include "macros/comand_types.h"

void hexdump(void *ptr, int buflen) {
	unsigned char *buf = (unsigned char*)ptr;
	int i, j;
	for (i=0; i<buflen; i+=16) {
		printf("%06x: ", i);
		for (j=0; j<16; j++)
			if (i+j < buflen)
				printf("%02x ", buf[i+j]);
			else
				printf("   ");
		printf(" ");
		for (j=0; j<16; j++)
			if (i+j < buflen)
				printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
		printf("\n");
	}
}

#pragma mark - Private definitions

#define USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN			(512)
#define USB_ACCESSORY_MESSAGE_PROCESSOR_BINARY_PACKET_SIGN			"!BIN"

#pragma mark - Private methods definitions

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_cmd(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_screen(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_hu_msg(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_control(int fd, common_header_t * header, uint8_t* buffer, size_t len);
usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_speech(int fd, common_header_t * header, uint8_t* buffer, size_t len);

#pragma mark - Internal methods implementations

usb_accessory_msg_processor_status_t usb_accessory_message_processor_setup(int acccessory_fd) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;

	uint8_t* buffer = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);

	if (!buffer) {
		status = USB_ACCESSORY_MSG_ERR_NO_MEM;

		goto out;
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

	while(write(acccessory_fd, buffer, USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN) < 0) {
		
		if (errno != 19) {
			status = USB_ACCESSORY_MSG_ERR_IO;
			
			break;
		}

		printf("Awaiting accessory ...\n");
		usleep(100000);
	}

flush:
	if (buffer)
		free(buffer);
out:
	return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle(int acccessory_fd) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
	uint8_t* read_buffer = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
	
	if (!read_buffer) {
		return USB_ACCESSORY_MSG_ERR_NO_MEM;
	}
	
	if (acccessory_fd < 0) {
		status = USB_ACCESSORY_MSG_ERR_BROKEN_FD;
		
		goto flush;
	}
	
	ssize_t readed = read(
		acccessory_fd,
		read_buffer,
		USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN
	);
	
	if (readed < 0) {
		status = USB_ACCESSORY_MSG_ERR_IO;
		
		goto flush;
	}

	hexdump(read_buffer, readed);
	
	common_header_t * header = (common_header_t*)read_buffer;
	
	if (memcmp(header->binary_mark, USB_ACCESSORY_MESSAGE_PROCESSOR_BINARY_PACKET_SIGN, 4) != 0) {
		goto flush;
	}
	
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
	} else if (data_type == MESSAGE_DATA_TYPE_HU_MSG) {
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
	
flush:
	free(read_buffer);
	
	return status;
}

#pragma mark - Private methods implementations

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_cmd(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
	common_comand_header_t * comand_header = (common_comand_header_t *)(buffer + sizeof(common_header_t));
	
	uint32_t cmd = ntohl(comand_header->cmd);
	uint8_t* response = NULL;
	
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
		printf("[MESSAGE_PROCESSOR] receive play status");
	} else if (cmd == MESSAGE_CMD_MIRROR_SUPPORT) {
		response = (uint8_t*)malloc(USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN);
		memcpy(response, buffer, len);
		
		mirror_support_t * mirror_support = (mirror_support_t *)response;
		
		mirror_support->ret = ntohl(1);
		mirror_support->screen_capture_support = ntohl(0);
	} else if (cmd == MESSAGE_CMD_RESEND_SPS) {
		printf("[MESSAGE_PROCESSOR] needs sps");
	}
	
	if (response) {
		
		if (write(fd, response, USB_ACCESSORY_MESSAGE_PROCESSOR_DEFAULT_PACKET_LEN) < 0) {
			status = USB_ACCESSORY_MSG_ERR_IO;
		}
		
		free(response);
	}
	
	return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_screen(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
	
	return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_hu_msg(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
	
	uint32_t full_packet_len = ntohl(header->total_size);
	uint8_t* response = (uint8_t*)malloc(full_packet_len);
	
	if (!response) {
		status = USB_ACCESSORY_MSG_ERR_NO_MEM;
		
		goto out;
	}
	
	memcpy(response, buffer, len);
	
	if (read(fd, response + len, full_packet_len - len) < 0) {
		status = USB_ACCESSORY_MSG_ERR_IO;
		
		goto flush;
	}
	
flush:
	if (response)
		free(response);
out:
	return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_control(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
	
	input_control_t* input_control = (input_control_t*)buffer;
	int16_t x1 = ntohl(input_control->event_value0);
	int16_t y1 = ntohl(input_control->event_value1);
	
	switch (ntohl(input_control->event_type)) {
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
	
	return status;
}

usb_accessory_msg_processor_status_t usb_accessory_message_processor_handle_speech(int fd, common_header_t * header, uint8_t* buffer, size_t len) {
	usb_accessory_msg_processor_status_t status = USB_ACCESSORY_MSG_OK;
	
	uint32_t full_packet_len = ntohl(header->total_size);
	uint8_t* response = (uint8_t*)malloc(full_packet_len);
	
	if (!response) {
		status = USB_ACCESSORY_MSG_ERR_NO_MEM;
		
		goto out;
	}
	
	memcpy(response, buffer, len);
	
	if (read(fd, response + len, full_packet_len - len) < 0) {
		status = USB_ACCESSORY_MSG_ERR_IO;
		
		goto flush;
	}
	
	speech_t * speech = (speech_t*)response;
	
	(void)speech;
	
flush:
	if (response)
		free(response);
out:
	return status;
}
