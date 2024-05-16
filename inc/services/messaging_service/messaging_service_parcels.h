#ifndef _MESSAGING_SERVICE_PARCELS_H_
#define _MESSAGING_SERVICE_PARCELS_H_

#include <stdint.h>

#pragma mark - Intranal definitions

#define MESSAGING_SERVICE_MARK                                  (0xFAFAFAFA)
#define MESSAGING_SERVICE_KEY_FRAME_REQ_EVENT                   (1)
#define MESSAGING_SERVICE_INPUT_EVENT                           (2)
#define MESSAGING_SERVICE_H264_FRAME_EVENT                      (3)

#define MESSAGING_SERVICE_INPUT_EVENT_DOWN                      (0)
#define MESSAGING_SERVICE_INPUT_EVENT_UP                        (1)
#define MESSAGING_SERVICE_INPUT_EVENT_MOVE                      (2)

#pragma mark - Intarnal types

typedef struct {
    uint32_t mark;
    uint32_t event_id;
    uint32_t full_len;
    uint32_t payload_len;
} __attribute__((packed)) messaging_service_parcel_header_t;

typedef struct {
    int16_t action;
    int16_t x;
    int16_t y;
} __attribute__((packed)) messaging_service_input_event_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
    uint32_t frame_size;
} __attribute__((packed)) messaging_service_video_event_t;

typedef struct {
    messaging_service_parcel_header_t header;
    messaging_service_input_event_t event;
} __attribute__((packed)) messaging_service_parcel_input_t;

typedef struct {
    messaging_service_parcel_header_t header;
    messaging_service_video_event_t event;
} __attribute__((packed)) messaging_service_video_frame_t;

#endif
