// Fake USB accessory layer for tests: the "head unit" side is a scripted byte queue,
// everything QDPlay writes is captured.

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "usb_accessory/usb_active_accessory.h"
#include "fake_usb.h"

static pthread_mutex_t fake_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint8_t fake_rx[1024 * 1024];
static size_t fake_rx_len = 0;
static size_t fake_rx_pos = 0;
// Every queued transfer ends at a boundary, like a USB bulk transfer
static size_t fake_rx_boundaries[1024];
static size_t fake_rx_boundaries_count = 0;
static size_t fake_rx_boundary_index = 0;

static uint8_t fake_tx[4 * 1024 * 1024];
static size_t fake_tx_len = 0;

static int fake_h264_frames = 0;
static size_t fake_h264_bytes = 0;

void fake_usb_reset(void) {
    pthread_mutex_lock(&fake_mutex);
    fake_rx_len = fake_rx_pos = 0;
    fake_rx_boundaries_count = fake_rx_boundary_index = 0;
    fake_tx_len = 0;
    fake_h264_frames = 0;
    fake_h264_bytes = 0;
    pthread_mutex_unlock(&fake_mutex);
}

void fake_usb_queue_transfer(const void *data, size_t len) {
    pthread_mutex_lock(&fake_mutex);
    memcpy(fake_rx + fake_rx_len, data, len);
    fake_rx_len += len;
    fake_rx_boundaries[fake_rx_boundaries_count++] = fake_rx_len;
    pthread_mutex_unlock(&fake_mutex);
}

size_t fake_usb_tx(const uint8_t **data) {
    *data = fake_tx;
    return fake_tx_len;
}

int fake_usb_h264_frames(void) {
    return __atomic_load_n(&fake_h264_frames, __ATOMIC_SEQ_CST);
}

int usb_active_accessory_read(void *buffer, size_t len) {
    int ret;

    pthread_mutex_lock(&fake_mutex);

    if (fake_rx_pos >= fake_rx_len) {
        pthread_mutex_unlock(&fake_mutex);
        errno = ENODEV;
        return -1;
    }

    // A read never crosses the end of the current transfer (short packet semantics)
    while (fake_rx_boundary_index < fake_rx_boundaries_count &&
           fake_rx_boundaries[fake_rx_boundary_index] <= fake_rx_pos) {
        fake_rx_boundary_index++;
    }

    size_t available = fake_rx_boundaries[fake_rx_boundary_index] - fake_rx_pos;

    if (len > available) {
        len = available;
    }

    memcpy(buffer, fake_rx + fake_rx_pos, len);
    fake_rx_pos += len;
    ret = (int)len;

    pthread_mutex_unlock(&fake_mutex);

    return ret;
}

int usb_active_accessory_read_message_tail(uint8_t *buffer, size_t received, size_t required, size_t capacity) {
    while (received < required) {
        int readed = usb_active_accessory_read(buffer + received, capacity - received);

        if (readed <= 0) {
            return -EIO;
        }

        received += readed;
    }

    return (int)received;
}

int usb_active_accessory_write(void *buffer, size_t len) {
    pthread_mutex_lock(&fake_mutex);
    if (fake_tx_len + len <= sizeof(fake_tx)) {
        memcpy(fake_tx + fake_tx_len, buffer, len);
        fake_tx_len += len;
    }
    pthread_mutex_unlock(&fake_mutex);

    return (int)len;
}

int usb_active_accessory_write_with_padding_512(void *header, size_t header_len, void *body, size_t body_len) {
    size_t total = header_len + body_len;
    size_t padded = (total + 511) & ~(size_t)511;
    uint8_t *tmp = calloc(1, padded);

    if (header_len) memcpy(tmp, header, header_len);
    if (body_len) memcpy(tmp + header_len, body, body_len);

    usb_active_accessory_write(tmp, padded);
    free(tmp);

    return (int)padded;
}

int usb_active_accessory_write_h264(message_processor_video_params_t video_parameters, void *buffer, size_t len) {
    (void)video_parameters;
    (void)buffer;

    __atomic_add_fetch(&fake_h264_frames, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&fake_h264_bytes, len, __ATOMIC_SEQ_CST);

    return (int)len;
}
