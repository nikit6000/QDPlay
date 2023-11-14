#ifndef _VIDEO_SENDER_H_
#define _VIDEO_SENDER_H_

void video_sender_start(int fd);
void video_sender_await_completion(void);
void video_sender_set_needs_sps(void);

#endif
