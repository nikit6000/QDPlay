#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <byteswap.h>
#include <string.h>
#include <errno.h>
#include "qd_control_receiver.h"

// TODO: - Move header to common place

#pragma mark - Private definitions

#define QD_MESSAGING_SERVICE_SOCKET_PATH                      "/tmp/qdplay.messaging.socket"
#define QD_MESSAGING_SERVICE_MARK                             (0xFAFAFAFA)
#define QD_MESSAGING_SERVICE_INITIAL_BUFFER_SIZE              (1024)
#define QD_MESSAGING_SERVICE_KEY_FRAME_REQ_EVENT              (1)
#define QD_MESSAGING_SERVICE_INPUT_EVENT                      (2)
#define QD_MESSAGING_RECONNECT_DELAY_US                       (250000) // 250 ms

#define QD_MESSAGING_SERVICE_INPUT_EVENT_DOWN                 (0)
#define QD_MESSAGING_SERVICE_INPUT_EVENT_UP                   (1)
#define QD_MESSAGING_SERVICE_INPUT_EVENT_MOVE                 (2)

#pragma mark - Private types

typedef struct {
    uint32_t mark;
    uint32_t event_id;
    uint32_t full_len;
    uint32_t payload_len;
} __attribute__((packed)) qd_control_parcel_header_t;

typedef struct {
    int16_t action;
    int16_t x;
    int16_t y;
} __attribute__((packed)) qd_contol_input_event_t;

typedef struct {
    qd_control_parcel_header_t header;
    qd_contol_input_event_t event;
} __attribute__((packed)) qd_control_parcel_input_t;

#pragma mark - Private properties

struct sockaddr_un qd_input_sink_name;
const char* qd_input_tag = "QD Messaging";
int qd_input_socket = -1;

#pragma mark - Private function prototypes

void qd_input_process_touch(qd_touch_callback touch_callback, qd_control_parcel_input_t * input);

#pragma mark - Open function implementations

int qd_input_reconnect(void) {
	int status = 0;

	do {
		if (qd_input_socket > 0) {
			close(qd_input_socket);
		}

		qd_input_socket = socket(AF_LOCAL, SOCK_STREAM, 0);

		if (qd_input_socket <= 0) {
			status = qd_input_socket;
			
			return -EIO;
		}

		qd_input_sink_name.sun_family = AF_LOCAL;
		strncpy(qd_input_sink_name.sun_path, QD_MESSAGING_SERVICE_SOCKET_PATH, sizeof(qd_input_sink_name.sun_path) - 1);

		status = connect(qd_input_socket, (const struct sockaddr*)&qd_input_sink_name, sizeof(qd_input_sink_name));

		if (status < 0) {
			usleep(QD_MESSAGING_RECONNECT_DELAY_US);
		}
	} while (status < 0);

	return 0;
}

int qd_input_process(
	qd_touch_callback touch_callback,
	qd_keyframe_callback keyframe_callback
) {
	static uint8_t *message_buffer = NULL;
	static size_t message_buffer_size = 0;
	qd_control_parcel_header_t* header = NULL;
	ssize_t received = 0;

	if (qd_input_socket <= 0) {
		return -EIO;
	}

	if (message_buffer == NULL) {
		message_buffer = (uint8_t*)malloc(QD_MESSAGING_SERVICE_INITIAL_BUFFER_SIZE);

		if (message_buffer == NULL) {
			return -ENOMEM;
		}

		message_buffer_size = QD_MESSAGING_SERVICE_INITIAL_BUFFER_SIZE;
	}

	received = recv(qd_input_socket, message_buffer, sizeof(qd_control_parcel_header_t), 0);

	if (received != sizeof(qd_control_parcel_header_t)) {
		printf("[%s] received incorrect data count! %d != %d\n", qd_input_tag, received, sizeof(qd_control_parcel_header_t));

		return -ECONNRESET;
	}

	header = (qd_control_parcel_header_t*)message_buffer;

	if (header->mark != bswap_32(QD_MESSAGING_SERVICE_MARK)) {
		printf("[%s] Bad binary mark!\n", qd_input_tag);

		return -EBADMSG;
	}

	uint32_t event_id = bswap_32(header->event_id);
	uint32_t payload_size = bswap_32(header->payload_len);
	uint32_t total_size = bswap_32(header->full_len);

	if (sizeof(qd_control_parcel_header_t) + payload_size != total_size) {
		printf("[%s] Bad message structure!\n ps: %d, ts: %d", qd_input_tag, payload_size, total_size);

		return -EBADMSG;
	}

	if (total_size > message_buffer_size) {
		uint8_t* resized_buffer = (uint8_t*)realloc(message_buffer, total_size);

		if (resized_buffer == NULL) {
			return -ENOMEM;
		}

		message_buffer = resized_buffer;
		message_buffer_size = total_size;
		header = (qd_control_parcel_header_t*)message_buffer;
	}

	if (payload_size > 0) {
		received = recv(qd_input_socket, message_buffer + sizeof(qd_control_parcel_header_t), payload_size, 0);
		
		if (received <= 0) {
			printf("[%s] Connection closed!\n", qd_input_tag);

			return -ECONNRESET;
		}
	}

	if (event_id == QD_MESSAGING_SERVICE_KEY_FRAME_REQ_EVENT) {
		keyframe_callback();
	} else if (event_id == QD_MESSAGING_SERVICE_INPUT_EVENT) {
		qd_input_process_touch(touch_callback, (qd_control_parcel_input_t*)message_buffer);
	}

	return 0;
}

#pragma mark - Private function implementations

void qd_input_process_touch(qd_touch_callback touch_callback, qd_control_parcel_input_t * input) {
	int16_t x1 = (int16_t)bswap_16(input->event.x);
	int16_t y1 = (int16_t)bswap_16(input->event.y);
	bool is_pressed = false;

	if (x1 < 0 || y1 < 0) {
		return;
	}

	switch ((int16_t)bswap_16(input->event.action)) {
	case QD_MESSAGING_SERVICE_INPUT_EVENT_DOWN:
		is_pressed = true;
		break;

	case QD_MESSAGING_SERVICE_INPUT_EVENT_MOVE:
		is_pressed = true;
		break;

	case QD_MESSAGING_SERVICE_INPUT_EVENT_UP:
		break;

	default:
		return;
	}

	touch_callback(is_pressed, x1, y1);
}