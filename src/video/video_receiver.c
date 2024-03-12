#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include "video/video_receiver.h"
#include "messages/screen_header.h"

#define VIDEO_RECEIVER_SOCKET_PATH                      "/tmp/qdplay.video.socket"
#define VIDEO_RECEIVER_SOCKET_MAXIMUM_CONN              (1)
#define VIDEO_RECEIVER_SOCKET_BUFFER_SIZE       		(1048576)
#define VIDEO_RECEIVER_PACKET_MARK                      "VIDEO"
#define VIDEO_RECEIVER_PACKET_MARK_LEN                  (5)

#pragma mark - Private types

typedef struct  {
	int fd;
	int buffer_size;
} video_receiver_context_t;

#pragma mark - Private propoerties

int video_sink_fd = -1;
pthread_t video_reveiver_thread_id;
pthread_mutex_t video_receiver_mutex;

#pragma mark - Private methods definitions

void* video_receiver_processing_thread(void * context);

#pragma mark - Internal methods implementation

video_receiver_status_t video_receiver_start(void) {
    struct sockaddr_un server_addr;
    int buffer_size = VIDEO_RECEIVER_SOCKET_BUFFER_SIZE;
	int double_buffer_size = 2 * buffer_size;
	int video_receiver_fd;

	pthread_mutex_init(&video_receiver_mutex, NULL);

    video_receiver_fd = socket(AF_UNIX, SOCK_DGRAM, 0);

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

void video_receiver_register_sink(int fd) {
	pthread_mutex_lock(&video_receiver_mutex);

	video_sink_fd = fd;

	pthread_mutex_unlock(&video_receiver_mutex);
}

void video_receiver_remove_sink(void) {
	video_receiver_register_sink(-1);
}

#pragma mark - Private methods implementation

void* video_receiver_processing_thread(void * context) {
	video_receiver_context_t *receiver_context =(video_receiver_context_t*)context;
	uint8_t* buffer;

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

	buffer = (uint8_t*)malloc(receiver_context->buffer_size);

	if (buffer == NULL) {
		close(receiver_context->fd);
		free(receiver_context);

		return NULL;
	}

    while (1) {
        int received = recv(receiver_context->fd, buffer, receiver_context->buffer_size, 0);

		pthread_mutex_lock(&video_receiver_mutex);

		if (received > 0 && video_sink_fd > 0) {
			write(video_sink_fd, buffer, received);
		}

		pthread_mutex_unlock(&video_receiver_mutex);
    }

	close(receiver_context->fd);
	free(receiver_context);
	free(buffer);

    return NULL;
}