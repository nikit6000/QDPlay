#include <byteswap.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "qd_video_sender.h"

#pragma mark - Private definitions

#define QD_MESSAGING_SERVICE_MARK                       (0xFAFAFAFA)
#define MESSAGING_SERVICE_H264_FRAME_EVENT              (3)
#define QD_SINK_SND_BUFFER_SIZE							(65535)
#define QD_VIDEO_RECEIVER_SOCKET_PATH					"/tmp/qdplay.video.socket"

#pragma mark - Private types

typedef struct {
    uint32_t mark;
    uint32_t event_id;
    uint32_t full_len;
    uint32_t payload_len;
} __attribute__((packed)) qd_sceen_parcel_header_t;

typedef struct {
	uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
    uint32_t frame_size;
} __attribute__((packed)) qd_sceen_parcel_video_event_t;

typedef struct {
    qd_sceen_parcel_header_t header;
    qd_sceen_parcel_video_event_t event;
} __attribute__((packed)) qd_screen_video_frame_t;

#pragma mark - Private properties 

const char* qd_screen_tag = "ScreenSender";

#pragma mark - Private function prototypes

int qd_screen_header_response_init(
	qd_screen_video_frame_t * obj,
    uint32_t width,
    uint32_t height,
    uint32_t frame_rate
);

#pragma mark - Open function implementations

qd_screen_impl_ref qd_screen_connect(uint32_t width, uint32_t height, uint32_t frame_rate) {
	int double_buffer_size = 2 * QD_SINK_SND_BUFFER_SIZE;
	struct sockaddr_un qd_video_sink_addr;
	qd_screen_impl_ref screen = NULL;

	// Init screen object

	screen = (qd_screen_impl_ref)malloc(sizeof(*screen));

	if (screen == NULL) {
		return NULL;
	}

	// Setup screen data

	screen->width = width;
	screen->height = height;
	screen->frame_rate = frame_rate;

	// Allocate initial buffer

	screen->buffer = (uint8_t*)malloc(QD_SINK_SND_BUFFER_SIZE);

	if (screen->buffer == NULL) {
		qd_screen_close(screen);

		return NULL;
	}

	screen->buffer_size = QD_SINK_SND_BUFFER_SIZE;

	// Connect to the video sink

	screen->sink_fd = socket(AF_LOCAL, SOCK_STREAM, 0);

	if (screen->sink_fd < 0) {
		qd_screen_close(screen);

		return NULL;
	}

	// Increace send buffer size

	if (setsockopt(screen->sink_fd, SOL_SOCKET, SO_SNDBUF, &double_buffer_size, sizeof(double_buffer_size)) < 0) {
		qd_screen_close(screen);

		return NULL;
	}

	memset(&qd_video_sink_addr, 0, sizeof(qd_video_sink_addr));

	qd_video_sink_addr.sun_family = AF_UNIX;

	strncpy(
		qd_video_sink_addr.sun_path, 
		QD_VIDEO_RECEIVER_SOCKET_PATH, 
		sizeof(qd_video_sink_addr.sun_path) - 1
	);

	if (connect(screen->sink_fd, (const struct sockaddr*)&qd_video_sink_addr, sizeof(qd_video_sink_addr)) < 0) {
		qd_screen_close(screen);

		printf("[%s]: Can`t connect to QD screen!\n", qd_screen_tag);

		return NULL;
	}

	printf("[%s]: Screen connected!\n", qd_screen_tag);

	printf(
		"[%s]: Screen parameters - w: %d, h: %d, fr: %d\n", 
		qd_screen_tag,
		screen->width,
		screen->height,
		screen->frame_rate
	);

	return screen;
}

int qd_screen_send_data(
	qd_screen_impl_ref screen,
	const uint8_t* data, 
	size_t len
) {
	if (screen == NULL) {
		printf("[%s]: Screen not inited!\n", qd_screen_tag);

		return -1;
	}

	if (len == 0) {
		printf("[%s]: Length is zero!\n", qd_screen_tag);

		return -1;
	}

	uint32_t header_size = sizeof(qd_screen_video_frame_t);
	uint32_t total_size = header_size + len;

	if (total_size > screen->buffer_size) {
		uint8_t* resized_buffer = (uint8_t*)realloc(screen->buffer, total_size);

		if (resized_buffer == NULL) {
			return -1;
		}

		screen->buffer = resized_buffer;
		screen->buffer_size = total_size;
	}

	// Cleanup buffer

	memset(screen->buffer, 0, total_size);

	// Map buffer to screen header

	qd_screen_video_frame_t * video_frame = (qd_screen_video_frame_t*)screen->buffer;

	// Initialize response header

	qd_screen_header_response_init(
		video_frame, 
		screen->width, 
		screen->height, 
		screen->frame_rate
	);

	// Fill data len

	video_frame->header.full_len = bswap_32(total_size);
	video_frame->header.payload_len = bswap_32(sizeof(qd_sceen_parcel_video_event_t) + len);
	video_frame->event.frame_size = bswap_32(len);

	// Copy video data

	memcpy(screen->buffer + header_size, data, len);

	// Send buffer to QD sink

	return send(
		screen->sink_fd, 
		screen->buffer,
		total_size,
		0
	);
}

void qd_screen_update(qd_screen_impl_ref screen, uint32_t width, uint32_t height) {
	if (screen == NULL) {
		return;
	}

	screen->width = width;
	screen->height = height;

	printf(
		"[%s]: Screen parameters now is - w: %d, h: %d, fr: %d\n", 
		qd_screen_tag,
		screen->width,
		screen->height,
		screen->frame_rate
	);
}

void qd_screen_close(qd_screen_impl_ref screen) {
	if (screen == NULL) {
		return;
	}

	if (screen->sink_fd > 0) {
		close(screen->sink_fd);
	}

	if (screen->buffer) {
		free(screen->buffer);
	}

	free(screen);
}

#pragma mark - Private function implementations

int qd_screen_header_response_init(
	qd_screen_video_frame_t * obj,
	uint32_t width,
	uint32_t height,
	uint32_t frame_rate
) {
	if (!obj) {
		return -1;
	}

	obj->header.mark = bswap_32(QD_MESSAGING_SERVICE_MARK); 
	obj->header.event_id = bswap_32(MESSAGING_SERVICE_H264_FRAME_EVENT);
	obj->header.payload_len = 0;
	obj->header.full_len = 0;
	obj->event.width = bswap_32(width);
	obj->event.height = bswap_32(height);
	obj->event.frame_rate = bswap_32(frame_rate);
	obj->event.frame_size = 0;

	return 0;
}