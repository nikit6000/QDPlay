#ifndef _VIDEO_RECEIVER_H_
#define _VIDEO_RECEIVER_H_

#include <glib.h>
#include <sys/types.h>

typedef enum {
    VIDEO_RECEIVER_OK = 0,
    VIDEO_RECEIVER_SOCKET_ERR,
    VIDEO_RECEIVER_SOCKET_BIND_ERR,
    VIDEO_RECEIVER_SOCKET_LISTEN_ERR,
	VIDEO_RECEIVER_NO_MEM_ERR
} video_receiver_status_t;

video_receiver_status_t video_receiver_start(void);
void video_receiver_activate(void);
void video_receiver_deactivate(void);
void video_receiver_await_connection(void);
gboolean video_receiver_is_connected(void);

// Several video sources (launcher, CarPlay, ...) may be connected at the same time.
// Only frames of the active one are forwarded to the head unit. The most recently
// connected source becomes active; when it disconnects, the previous one takes over.

pid_t video_receiver_active_pid(void);
size_t video_receiver_sources_count(void);
void video_receiver_request_active(pid_t pid);
void video_receiver_yield(pid_t pid);
void video_receiver_switch_to_previous(void);

#endif
