#ifndef _VIDEO_RECEIVER_H_
#define _VIDEO_RECEIVER_H_

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

#endif
