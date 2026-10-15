#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <logging/logging.h>
#include <byteswap.h>
#include "macros/data_types.h"
#include "usb_accessory/usb_active_accessory.h"
#include "services/video_receiver/video_receiver.h"
#include "services/messaging_service/messaging_service.h"
#include "services/messaging_service/messaging_service_parcels.h"

#define VIDEO_RECEIVER_SOCKET_PATH                      "/tmp/qdplay.video.socket"
#define VIDEO_RECEIVER_MAX_SOURCES                      (4)
#define VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE       	(65535)
#define VIDEO_RECEIVER_INITIAL_BUFFER_SIZE              (256 * 1024)
// Sanity limit for a single H.264 access unit
#define VIDEO_RECEIVER_MAX_FRAME_SIZE                   (8 * 1024 * 1024)
// After a source switch frames are held back until SPS/IDR, but not longer than this
#define VIDEO_RECEIVER_KEYFRAME_WAIT_US                 (1500000)

#pragma mark - Private types

typedef struct {
	int fd;
	pid_t pid;
	uint64_t priority;
	uint8_t *buffer;
	size_t buffer_size;
	size_t filled;
} video_receiver_source_t;

typedef struct {
	int listen_fd;
	pthread_t thread_id;
	pthread_mutex_t mutex;
	pthread_cond_t conn_cond;
	video_receiver_source_t sources[VIDEO_RECEIVER_MAX_SOURCES];
	size_t sources_count;
	uint64_t next_priority;
	pid_t active_pid;
	gboolean sink_active;
	// The decoder of the head unit can not continue with P-frames of another stream
	gboolean awaiting_keyframe;
	gint64 awaiting_keyframe_since;
} video_receiver_state_t;

#pragma mark - Private propoerties

static video_receiver_state_t video_receiver_state = {
	.listen_fd = -1,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.conn_cond = PTHREAD_COND_INITIALIZER,
	.sources_count = 0,
	.next_priority = 1,
	.active_pid = 0,
	.sink_active = FALSE
};

static const gchar* video_receiver_tag = "VideoReceiver";

#pragma mark - Private methods definitions

static void* video_receiver_processing_thread(void * context);
static void video_receiver_accept(void);
static gboolean video_receiver_read_source(video_receiver_source_t *source);
static void video_receiver_remove_source(size_t index);
static pid_t video_receiver_pick_active_locked(void);
static void video_receiver_update_active_locked(pid_t *old_pid, pid_t *new_pid);
static void video_receiver_notify_active_change(pid_t old_pid, pid_t new_pid);

#pragma mark - Internal methods implementation

video_receiver_status_t video_receiver_start(void) {
    struct sockaddr_un server_addr;
	int double_buffer_size = 2 * VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE;
	int video_receiver_fd;

    video_receiver_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (video_receiver_fd < 0) {
        return VIDEO_RECEIVER_SOCKET_ERR;
    }

	if (setsockopt(video_receiver_fd, SOL_SOCKET, SO_RCVBUF, &double_buffer_size, sizeof(int)) < 0) {
		close(video_receiver_fd);

		return VIDEO_RECEIVER_SOCKET_ERR;
	}

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sun_family = AF_UNIX;

    strncpy(server_addr.sun_path, VIDEO_RECEIVER_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    unlink(VIDEO_RECEIVER_SOCKET_PATH);

    if (bind(video_receiver_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(video_receiver_fd);

        return VIDEO_RECEIVER_SOCKET_BIND_ERR;
    }

	if (listen(video_receiver_fd, VIDEO_RECEIVER_MAX_SOURCES) < 0) {
		close(video_receiver_fd);

        return VIDEO_RECEIVER_SOCKET_LISTEN_ERR;
	}

	video_receiver_state.listen_fd = video_receiver_fd;

	if (pthread_create(&video_receiver_state.thread_id, NULL, video_receiver_processing_thread, NULL) != 0) {
		close(video_receiver_fd);

		video_receiver_state.listen_fd = -1;

		return VIDEO_RECEIVER_SOCKET_ERR;
	}

    return VIDEO_RECEIVER_OK;
}

void video_receiver_activate(void) {
	pthread_mutex_lock(&video_receiver_state.mutex);

	video_receiver_state.sink_active = TRUE;

	pthread_mutex_unlock(&video_receiver_state.mutex);
}

void video_receiver_deactivate(void) {
	pthread_mutex_lock(&video_receiver_state.mutex);

	video_receiver_state.sink_active = FALSE;

	pthread_mutex_unlock(&video_receiver_state.mutex);
}

void video_receiver_await_connection(void) {
	pthread_mutex_lock(&video_receiver_state.mutex);

	while (video_receiver_state.sources_count == 0)
	{
		pthread_cond_wait(&video_receiver_state.conn_cond, &video_receiver_state.mutex);
	}

	pthread_mutex_unlock(&video_receiver_state.mutex);
}

gboolean video_receiver_is_connected(void) {
	gboolean connected;

	pthread_mutex_lock(&video_receiver_state.mutex);

	connected = video_receiver_state.sources_count > 0;

	pthread_mutex_unlock(&video_receiver_state.mutex);

	return connected;
}

size_t video_receiver_sources_count(void) {
	size_t count;

	pthread_mutex_lock(&video_receiver_state.mutex);

	count = video_receiver_state.sources_count;

	pthread_mutex_unlock(&video_receiver_state.mutex);

	return count;
}

pid_t video_receiver_active_pid(void) {
	pid_t pid;

	pthread_mutex_lock(&video_receiver_state.mutex);

	pid = video_receiver_state.active_pid;

	pthread_mutex_unlock(&video_receiver_state.mutex);

	return pid;
}

void video_receiver_request_active(pid_t pid) {
	pid_t old_pid = 0, new_pid = 0;

	pthread_mutex_lock(&video_receiver_state.mutex);

	for (size_t i = 0; i < video_receiver_state.sources_count; i++) {
		if (video_receiver_state.sources[i].pid == pid) {
			video_receiver_state.sources[i].priority = video_receiver_state.next_priority++;
		}
	}

	video_receiver_update_active_locked(&old_pid, &new_pid);

	pthread_mutex_unlock(&video_receiver_state.mutex);

	video_receiver_notify_active_change(old_pid, new_pid);
}

void video_receiver_yield(pid_t pid) {
	pid_t old_pid = 0, new_pid = 0;

	pthread_mutex_lock(&video_receiver_state.mutex);

	// Lower the priority of every stream of this process and raise the best of the others.
	// Only lowering is not enough: after a few switches both sources end up with priority 0
	// and the tie keeps the yielding one on screen.

	video_receiver_source_t *next = NULL;

	for (size_t i = 0; i < video_receiver_state.sources_count; i++) {
		video_receiver_source_t *source = &video_receiver_state.sources[i];

		if (source->pid == pid) {
			source->priority = 0;
		} else if (next == NULL || source->priority > next->priority) {
			next = source;
		}
	}

	if (next != NULL) {
		next->priority = video_receiver_state.next_priority++;
	}

	video_receiver_update_active_locked(&old_pid, &new_pid);

	pthread_mutex_unlock(&video_receiver_state.mutex);

	video_receiver_notify_active_change(old_pid, new_pid);
}

void video_receiver_switch_to_previous(void) {
	pid_t active_pid = video_receiver_active_pid();

	if (active_pid != 0) {
		video_receiver_yield(active_pid);
	}
}

#pragma mark - Private methods implementation

static pid_t video_receiver_pick_active_locked(void) {
	video_receiver_source_t *best = NULL;

	for (size_t i = 0; i < video_receiver_state.sources_count; i++) {
		video_receiver_source_t *source = &video_receiver_state.sources[i];

		if (best == NULL || source->priority > best->priority) {
			best = source;
		}
	}

	return best ? best->pid : 0;
}

static void video_receiver_update_active_locked(pid_t *old_pid, pid_t *new_pid) {
	*old_pid = video_receiver_state.active_pid;
	*new_pid = video_receiver_pick_active_locked();

	video_receiver_state.active_pid = *new_pid;

	if (*old_pid != *new_pid) {
		video_receiver_state.awaiting_keyframe = TRUE;
		video_receiver_state.awaiting_keyframe_since = g_get_monotonic_time();
	}
}

// True if the Annex B access unit contains SPS (7) or an IDR slice (5)
static gboolean video_receiver_is_keyframe(const uint8_t *data, size_t len) {
	for (size_t i = 0; i + 3 < len; i++) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
			uint8_t nal_type = data[i + 3] & 0x1F;

			if (nal_type == 7 || nal_type == 5) {
				return TRUE;
			}

			i += 2;
		}
	}

	return FALSE;
}

static void video_receiver_notify_active_change(pid_t old_pid, pid_t new_pid) {
	if (old_pid != new_pid) {
		LOG_I(video_receiver_tag, "Active video source changed: %d -> %d", old_pid, new_pid);
	}

	// Sent on every change of the sources list, not only when the active one changes
	messaging_service_notify_sources(old_pid, new_pid, video_receiver_sources_count());
}

static void video_receiver_accept(void) {
	pid_t old_pid = 0, new_pid = 0;
	struct ucred credentials = { 0 };
	socklen_t credentials_len = sizeof(credentials);
	int source_fd = accept4(video_receiver_state.listen_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);

	if (source_fd < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			LOG_E(video_receiver_tag, "Can`t accept video source: %s", strerror(errno));

			// Do not spin on persistent errors like EMFILE
			usleep(100000);
		}

		return;
	}

	if (getsockopt(source_fd, SOL_SOCKET, SO_PEERCRED, &credentials, &credentials_len) < 0) {
		credentials.pid = 0;
	}

	pthread_mutex_lock(&video_receiver_state.mutex);

	if (video_receiver_state.sources_count >= VIDEO_RECEIVER_MAX_SOURCES) {
		pthread_mutex_unlock(&video_receiver_state.mutex);

		LOG_E(video_receiver_tag, "Too many video sources, rejecting pid %d", credentials.pid);

		close(source_fd);

		return;
	}

	video_receiver_source_t *source = &video_receiver_state.sources[video_receiver_state.sources_count];

	memset(source, 0, sizeof(*source));

	source->fd = source_fd;
	source->pid = credentials.pid;
	source->priority = video_receiver_state.next_priority++;

	video_receiver_state.sources_count++;

	video_receiver_update_active_locked(&old_pid, &new_pid);

	pthread_cond_broadcast(&video_receiver_state.conn_cond);

	pthread_mutex_unlock(&video_receiver_state.mutex);

	LOG_I(video_receiver_tag, "Video source connected (pid: %d)", credentials.pid);

	video_receiver_notify_active_change(old_pid, new_pid);
}

static void video_receiver_remove_source(size_t index) {
	pid_t old_pid = 0, new_pid = 0, pid = 0;

	pthread_mutex_lock(&video_receiver_state.mutex);

	if (index >= video_receiver_state.sources_count) {
		pthread_mutex_unlock(&video_receiver_state.mutex);

		return;
	}

	video_receiver_source_t *source = &video_receiver_state.sources[index];

	pid = source->pid;

	close(source->fd);
	free(source->buffer);

	video_receiver_state.sources[index] = video_receiver_state.sources[--video_receiver_state.sources_count];

	video_receiver_update_active_locked(&old_pid, &new_pid);

	pthread_cond_broadcast(&video_receiver_state.conn_cond);

	pthread_mutex_unlock(&video_receiver_state.mutex);

	LOG_I(video_receiver_tag, "Video source disconnected (pid: %d)", pid);

	video_receiver_notify_active_change(old_pid, new_pid);
}

static gboolean video_receiver_handle_frame(video_receiver_source_t *source, messaging_service_video_frame_t *frame, size_t full_len) {
	size_t frame_size = full_len - sizeof(messaging_service_video_frame_t);
	gboolean forward;

	if (frame_size != bswap_32(frame->event.frame_size)) {
		LOG_E(
			video_receiver_tag,
			"Calculated frame size not matched with declared size: %zu != %u",
			frame_size,
			bswap_32(frame->event.frame_size)
		);

		return FALSE;
	}

	pthread_mutex_lock(&video_receiver_state.mutex);

	forward = video_receiver_state.sink_active && video_receiver_state.active_pid == source->pid;

	if (forward && video_receiver_state.awaiting_keyframe) {
		const uint8_t *payload = (const uint8_t*)frame + sizeof(messaging_service_video_frame_t);
		gboolean timed_out = g_get_monotonic_time() - video_receiver_state.awaiting_keyframe_since > VIDEO_RECEIVER_KEYFRAME_WAIT_US;

		if (video_receiver_is_keyframe(payload, frame_size) || timed_out) {
			if (timed_out) {
				LOG_W(video_receiver_tag, "No key frame from pid %d, forwarding anyway", source->pid);
			}

			video_receiver_state.awaiting_keyframe = FALSE;
		} else {
			forward = FALSE;
		}
	}

	pthread_mutex_unlock(&video_receiver_state.mutex);

	if (forward && frame_size > 0) {
		message_processor_video_params_t message_parameters = {
			.width = bswap_32(frame->event.width),
			.height = bswap_32(frame->event.height),
			.frame_rate = bswap_32(frame->event.frame_rate)
		};

		usb_active_accessory_write_h264(
			message_parameters,
			(uint8_t*)frame + sizeof(messaging_service_video_frame_t),
			frame_size
		);
	}

	return TRUE;
}

static gboolean video_receiver_read_source(video_receiver_source_t *source) {
	// Drain everything that is available, then process complete frames

	while (1) {
		if (source->buffer_size - source->filled < VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE) {
			size_t new_size = source->buffer_size ? source->buffer_size * 2 : VIDEO_RECEIVER_INITIAL_BUFFER_SIZE;

			if (new_size > VIDEO_RECEIVER_MAX_FRAME_SIZE + VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE * 2) {
				LOG_E(video_receiver_tag, "Video source buffer overflow");

				return FALSE;
			}

			uint8_t *resized = (uint8_t*)realloc(source->buffer, new_size);

			if (resized == NULL) {
				LOG_E(video_receiver_tag, "Failed to allocate memory!");

				return FALSE;
			}

			source->buffer = resized;
			source->buffer_size = new_size;
		}

		ssize_t received = recv(source->fd, source->buffer + source->filled, source->buffer_size - source->filled, 0);

		if (received == 0) {
			return FALSE;
		}

		if (received < 0) {
			if (errno == EINTR) {
				continue;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}

			LOG_E(video_receiver_tag, "Can`t read data: %s", strerror(errno));

			return FALSE;
		}

		source->filled += received;

		// Process every complete frame in the buffer

		size_t offset = 0;

		while (source->filled - offset >= sizeof(messaging_service_video_frame_t)) {
			messaging_service_video_frame_t *frame = (messaging_service_video_frame_t*)(source->buffer + offset);

			if (frame->header.mark != bswap_32(MESSAGING_SERVICE_MARK)) {
				LOG_E(video_receiver_tag, "Message binary mark not matched");

				return FALSE;
			}

			if (frame->header.event_id != bswap_32(MESSAGING_SERVICE_H264_FRAME_EVENT)) {
				LOG_E(video_receiver_tag, "Message has wrong event_id");

				return FALSE;
			}

			size_t full_len = bswap_32(frame->header.full_len);

			if (full_len < sizeof(messaging_service_video_frame_t) || full_len > VIDEO_RECEIVER_MAX_FRAME_SIZE) {
				LOG_E(video_receiver_tag, "Bad frame length: %zu", full_len);

				return FALSE;
			}

			if (source->filled - offset < full_len) {
				// Make sure the whole frame will fit

				if (offset == 0 && full_len > source->buffer_size) {
					uint8_t *resized = (uint8_t*)realloc(source->buffer, full_len + VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE);

					if (resized == NULL) {
						return FALSE;
					}

					source->buffer = resized;
					source->buffer_size = full_len + VIDEO_RECEIVER_SOCKET_RCV_BUFFER_SIZE;
				}

				break;
			}

			if (video_receiver_handle_frame(source, frame, full_len) == FALSE) {
				return FALSE;
			}

			offset += full_len;
		}

		if (offset > 0) {
			memmove(source->buffer, source->buffer + offset, source->filled - offset);

			source->filled -= offset;
		}
	}

	return TRUE;
}

static void* video_receiver_processing_thread(void * context) {
	struct pollfd poll_fds[VIDEO_RECEIVER_MAX_SOURCES + 1];

	(void)context;

	LOG_I(video_receiver_tag, "Awaiting for video sources ...");

    while (1) {
		size_t count = 0;
		int fds_snapshot[VIDEO_RECEIVER_MAX_SOURCES];

		poll_fds[0].fd = video_receiver_state.listen_fd;
		poll_fds[0].events = POLLIN;

		// Only this thread changes the sources list, so the snapshot stays valid until the next iteration

		pthread_mutex_lock(&video_receiver_state.mutex);

		count = video_receiver_state.sources_count;

		for (size_t i = 0; i < count; i++) {
			fds_snapshot[i] = video_receiver_state.sources[i].fd;
			poll_fds[i + 1].fd = fds_snapshot[i];
			poll_fds[i + 1].events = POLLIN;
		}

		pthread_mutex_unlock(&video_receiver_state.mutex);

		if (poll(poll_fds, count + 1, -1) < 0) {
			if (errno != EINTR) {
				LOG_E(video_receiver_tag, "poll failed: %s", strerror(errno));

				usleep(100000);
			}

			continue;
		}

		// Iterate backwards: removal moves the last source into the removed slot

		for (size_t i = count; i > 0; i--) {
			short revents = poll_fds[i].revents;

			if (revents == 0) {
				continue;
			}

			if (video_receiver_read_source(&video_receiver_state.sources[i - 1]) == FALSE) {
				video_receiver_remove_source(i - 1);
			}
		}

		if (poll_fds[0].revents & POLLIN) {
			video_receiver_accept();
		}
    }

    return NULL;
}
