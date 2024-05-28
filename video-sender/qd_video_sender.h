#ifndef _QD_SCREEN_SENDER_H_
#define _QD_SCREEN_SENDER_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct qd_screen_impl* qd_screen_impl_ref;
struct qd_screen_impl {
    int sink_fd;
    uint8_t* buffer;
    size_t buffer_size;
    uint32_t width;
	uint32_t height;
	uint32_t frame_rate;
};


qd_screen_impl_ref qd_screen_connect(uint32_t width, uint32_t height, uint32_t frame_rate);
int qd_screen_send_data(qd_screen_impl_ref screen, const uint8_t* data, size_t len);
void qd_screen_update(qd_screen_impl_ref screen, uint32_t width, uint32_t height);
void qd_screen_close(qd_screen_impl_ref screen);

#endif
