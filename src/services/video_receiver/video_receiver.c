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
#include "messages/v1/screen_header.h"
#include "usb_accessory/usb_active_accessory.h"
#include "services/video_receiver/video_receiver.h"
#include "services/messaging_service/messaging_service_parcels.h"

#define VIDEO_RECEIVER_SOCKET_PATH                      "/tmp/qdplay.video.socket"
#define VIDEO_RECEIVER_SOCKET_MAXIMUM_CONN              (1)
#define VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE       	(65535)
#define VIDEO_RECEIVER_SOCKET_HEADER_SIZE				MESSAGE_DEFAULT_CHUNK_SIZE

#pragma mark - Private types

typedef struct {
	int fd;
	int buffer_size;
} video_receiver_context_t;

#pragma mark - Private propoerties

gboolean video_sink_active = FALSE;
gboolean video_receiver_connected = FALSE;
pthread_t video_reveiver_thread_id;
pthread_mutex_t video_receiver_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t video_receiver_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t video_receiver_conn_cond = PTHREAD_COND_INITIALIZER;
static gchar* video_receiver_tag = "VideoReceicer";

void DumpHex(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

#pragma mark - Private methods definitions

void* video_receiver_processing_thread(void * context);

#pragma mark - Internal methods implementation

video_receiver_status_t video_receiver_start(void) {
    struct sockaddr_un server_addr;
    int buffer_size = VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE;
	int double_buffer_size = 2 * buffer_size;
	int video_receiver_fd;

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

void video_receiver_await_connection(void) {
	pthread_mutex_lock(&video_receiver_conn_mutex);

	while (video_receiver_connected == FALSE)
	{
		pthread_cond_wait(&video_receiver_conn_cond, &video_receiver_conn_mutex);
	}

	pthread_mutex_unlock(&video_receiver_conn_mutex);
}

gboolean video_receiver_is_connected(void) {
	return video_receiver_connected;
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
		LOG_E(video_receiver_tag, "Can`t video to video source");

		return;
	}

	LOG_I(video_receiver_tag, "Video source connected!");

	pthread_mutex_lock(&video_receiver_conn_mutex);

	video_receiver_connected = TRUE;
	pthread_cond_broadcast(&video_receiver_conn_cond);

	pthread_mutex_unlock(&video_receiver_conn_mutex);

	while (1) {
		messaging_service_video_frame_t* video_frame = NULL;
		size_t data_to_read = 0;
		size_t calculated_video_frame_size = 0;
		ssize_t received_data_len = recv(video_source_fd, *buffer, sizeof(messaging_service_video_frame_t), 0);

		if (received_data_len <= 0) {
			LOG_E(video_receiver_tag, "Can`t read data: %d", received_data_len);

			break;
		}

		if (received_data_len != sizeof(messaging_service_video_frame_t)) {
			LOG_E(
				video_receiver_tag, 
				"Sequence error, initial package has incorrect size. %d != %d", 
				received_data_len,
				sizeof(messaging_service_video_frame_t)
			);

			break;
		}

		video_frame = (messaging_service_video_frame_t*)(*buffer);

		if (video_frame->header.mark != bswap_32(MESSAGING_SERVICE_MARK)) {
			LOG_E(video_receiver_tag, "Message binary mark not matched");

			break;
		}

		if (video_frame->header.event_id != bswap_32(MESSAGING_SERVICE_H264_FRAME_EVENT)) {
			LOG_E(video_receiver_tag, "Message has wrong event_id");

			break;
		}

		// Determinate full package size

		data_to_read = bswap_32(video_frame->header.full_len);

		// Resize buffer if needed

		if (data_to_read > *buffer_size) {
			uint8_t* resized_buffer = (uint8_t*)realloc(*buffer, data_to_read);

			if (resized_buffer == NULL) {
				LOG_E(video_receiver_tag, "Failed to allocate memory!");

				break;
			}

			*buffer_size = data_to_read;
			*buffer = resized_buffer;
			video_frame = (messaging_service_video_frame_t*)(*buffer);
		}

		// Substract already received data count

		data_to_read -= received_data_len;

		while (data_to_read > 0)
		{
			uint8_t* buffer_base_ptr = *buffer;
			ssize_t readed = recv(video_source_fd, buffer_base_ptr + received_data_len, data_to_read, 0);

			if (readed <= 0) {
				break;
			}

			data_to_read -= readed;
			received_data_len += readed;
		}

		if (data_to_read != 0) {
			LOG_E(
				video_receiver_tag, 
				"Failed to read data chunks. Remaining data: %d",
				data_to_read
			);

			break;
		}

		calculated_video_frame_size = received_data_len - sizeof(messaging_service_video_frame_t);

		if (calculated_video_frame_size != bswap_32(video_frame->event.frame_size)) {
			LOG_E(
				video_receiver_tag, 
				"Calculated frame size not matched with declared size: %d (%d - %d) != %d, full package size: 0x%X",
				calculated_video_frame_size,
				received_data_len,
				sizeof(messaging_service_video_frame_t),
				bswap_32(video_frame->event.frame_size),
				bswap_32(video_frame->header.full_len)
			);

			break;
		}

		if (received_data_len > 0 && video_sink_active == TRUE) {

			message_processor_video_params_t message_parameters = {
				.width = bswap_32(video_frame->event.width),
				.height = bswap_32(video_frame->event.height),
				.frame_rate = bswap_32(video_frame->event.frame_rate)
			};

			usb_active_accessory_write_h264(
				message_parameters,
				(void*)(*buffer + sizeof(messaging_service_video_frame_t)),
				calculated_video_frame_size
			);
		}
	}

	close(video_source_fd);

	LOG_I(video_receiver_tag, "Video source disconnected!");

	pthread_mutex_lock(&video_receiver_conn_mutex);

	video_receiver_connected = FALSE;
	pthread_cond_broadcast(&video_receiver_conn_cond);

	pthread_mutex_unlock(&video_receiver_conn_mutex);
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