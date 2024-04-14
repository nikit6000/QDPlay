#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <logging/logging.h>
#include <byteswap.h>
#include "macros/data_types.h"
#include "video/video_receiver.h"
#include "messages/screen_header.h"
#include "usb_accessory/usb_active_accessory.h"

#define VIDEO_RECEIVER_SOCKET_PATH                      "/tmp/qdplay.video.socket"
#define VIDEO_RECEIVER_SOCKET_MAXIMUM_CONN              (1)
#define VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE       	(65535)
#define VIDEO_RECEIVER_SOCKET_HEADER_SIZE				MESSAGE_DEFAULT_CHUNK_SIZE

#pragma mark - Private types

typedef struct  {
	int fd;
	int buffer_size;
} video_receiver_context_t;

#pragma mark - Private propoerties

gboolean video_sink_active = FALSE;
pthread_t video_reveiver_thread_id;
pthread_mutex_t video_receiver_mutex;
static gchar* video_receiver_tag = "VideoReceicer";

#pragma mark - Private methods definitions

void* video_receiver_processing_thread(void * context);

#pragma mark - Internal methods implementation

video_receiver_status_t video_receiver_start(void) {
    struct sockaddr_un server_addr;
    int buffer_size = VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE;
	int double_buffer_size = 2 * buffer_size;
	int video_receiver_fd;

	pthread_mutex_init(&video_receiver_mutex, NULL);

    video_receiver_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (video_receiver_fd < 0) {
        return VIDEO_RECEIVER_SOCKET_ERR;
    }

	if (setsockopt(video_receiver_fd, SOL_SOCKET, SO_RCVBUF, &double_buffer_size, sizeof(int)) < 0) {
		return VIDEO_RECEIVER_SOCKET_ERR;
	} 

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sun_family = AF_UNIX;

    strcpy(server_addr.sun_path, VIDEO_RECEIVER_SOCKET_PATH);

    unlink(VIDEO_RECEIVER_SOCKET_PATH);

    if (bind(video_receiver_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(video_receiver_fd);

        return VIDEO_RECEIVER_SOCKET_BIND_ERR;
    }

	if (listen(video_receiver_fd, VIDEO_RECEIVER_SOCKET_MAXIMUM_CONN) < 0) {
		close(video_receiver_fd);

        return VIDEO_RECEIVER_SOCKET_LISTEN_ERR;
	}

	video_receiver_context_t* context = (video_receiver_context_t*)malloc(sizeof(video_receiver_context_t));

	if (context == NULL) {
		close(video_receiver_fd);

		return VIDEO_RECEIVER_NO_MEM_ERR;
	}

	context->fd = video_receiver_fd;
	context->buffer_size = buffer_size;

	pthread_create(
		&video_reveiver_thread_id, 
		NULL,
		video_receiver_processing_thread,
		(void*)context
	);

    return VIDEO_RECEIVER_OK;
}

void video_receiver_activate(void) {
	pthread_mutex_lock(&video_receiver_mutex);

	video_sink_active = TRUE;

	pthread_mutex_unlock(&video_receiver_mutex);
}

void video_receiver_deactivate(void) {
	pthread_mutex_lock(&video_receiver_mutex);

	video_sink_active = FALSE;

	pthread_mutex_unlock(&video_receiver_mutex);
}

#pragma mark - Private methods implementation

void video_receiver_handle_client(int fd, uint8_t** buffer, size_t *buffer_size) {
	struct sockaddr_un video_source_addr;
	socklen_t video_source_addr_len;

	if (buffer == NULL || *buffer == NULL || buffer_size == NULL || *buffer_size < VIDEO_RECEIVER_SOCKET_HEADER_SIZE) {
		return;
	}

	LOG_I(video_receiver_tag, "Awaiting for video source ...");

	int video_source_fd = accept(fd, (struct sockaddr*)&video_source_addr, &video_source_addr_len);

	if (video_source_fd < 0) {
		LOG_I(video_receiver_tag, "Can`t video to video source");

		return;
	}

	LOG_I(video_receiver_tag, "Video ource connected!");

	while (1) {
		screen_header_t* header = NULL;
		size_t data_to_read = 0;
		int received_data_len = recv(video_source_fd, *buffer, *buffer_size, 0);
		int size_difference = 0;

		if (received_data_len <= 0) {
			break;
		}

		header = (screen_header_t*)buffer;

		if (memcmp(header->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN) != 0) {
			break;
		}

		data_to_read = bswap_32(header->common_header.total_size);

		if (data_to_read > *buffer_size) {
			uint8_t* resized_buffer = (uint8_t*)realloc(*buffer, data_to_read);

			if (resized_buffer == NULL) {
				LOG_E(video_receiver_tag, "Failed to allocate memory!");

				break;
			}

			*buffer_size = data_to_read;
			*buffer = resized_buffer;
		}

		data_to_read -= received_data_len;

		while (data_to_read > 0)
		{
			int readed = recv(video_source_fd, *buffer + received_data_len, data_to_read, 0);

			if (readed <= 0) {
				break;
			}

			data_to_read -= readed;
			received_data_len += readed;
		}

		if (data_to_read) {
			break;
		}

		size_difference = received_data_len >> MESSAGE_DEFAULT_CHUNK_LOG2;
		size_difference = size_difference << MESSAGE_DEFAULT_CHUNK_LOG2;

		if (received_data_len - size_difference) {
			LOG_W(video_receiver_tag, "Received data is not aligned!");

			continue;
		}

		pthread_mutex_lock(&video_receiver_mutex);

		if (received_data_len > 0 && video_sink_active == TRUE) {
			usb_active_accessory_write(*buffer, received_data_len);
		}

		pthread_mutex_unlock(&video_receiver_mutex);
	}

	close(video_source_fd);

	LOG_I(video_receiver_tag, "Video source disconnected!");
}

void* video_receiver_processing_thread(void * context) {
	video_receiver_context_t *receiver_context =(video_receiver_context_t*)context;
	uint8_t* buffer = NULL;
	size_t buffer_size = 0;

	if (receiver_context == NULL) {
		return NULL;
	}

	if (receiver_context->fd <= 0) {
		free(receiver_context);

		return NULL;
	}

	if (receiver_context->buffer_size <= 0) {
		close(receiver_context->fd);
		free(receiver_context);

		return NULL;
	}

	buffer_size = receiver_context->buffer_size;
	buffer = (uint8_t*)malloc(buffer_size);

	if (buffer == NULL) {
		close(receiver_context->fd);
		free(receiver_context);

		return NULL;
	}

    while (1) {
        video_receiver_handle_client(receiver_context->fd, &buffer, &buffer_size);
    }

	close(receiver_context->fd);
	free(receiver_context);
	free(buffer);

    return NULL;
}