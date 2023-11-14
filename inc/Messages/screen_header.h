#ifndef _SCREEN_HEADER_H_
#define _SCREEN_HEADER_H_

#include "common_header.h"

#pragma mark - Internal definitions

#define SCREEN_HEADER_VIDEO_FORMAT_H264					(3)

#pragma mark - Internal types

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ uint32_t capture_width;  					// 1920
	/* 068 */ uint32_t capture_height; 					// 590
	/* 072 */ uint32_t pixel_format;					// 4
	/* 076 */ uint32_t pixel_size;						// 2
	/* 080 */ uint32_t encoding_type;					// 3
	/* 084 */ uint32_t screen_orientation;				// 0
	/* 088 */ uint32_t screen_direct;					// 1
	/* 092 */ uint32_t capture_data_len;				// h264 frame len
	/* 096 */ uint32_t no;								// 0
	/* 100 */ uint32_t screen_width;					// 1920
	/* 104 */ uint32_t screen_height;					// 590
	/* 108 */ uint32_t screen_pixel_format;				// 0
	/* 112 */ uint32_t screen_pixel_size;				// 0
	/* 116 */ uint32_t screen_capture_width;			// 1920
	/* 120 */ uint32_t screen_capture_height;			// 590
	/* 124 */ uint32_t screen_capture_pixel_format;		// 0
	/* 128 */ uint32_t screen_captire_pixel_size;		// 0
	/* 132 */ uint32_t screen_capture_encoding_type;	// 3
	/* 136 */ uint32_t screen_capture_orientation;		// 90
	/* 140 */ uint32_t screen_capture_direct;			// 1
	/* 144 */ uint32_t screen_capture_number_of_screens;// 0
	/* 148 */ uint32_t screen_width_0;					// 1920
	/* 152 */ uint32_t screen_height_0;					// 1080
	/* 156 */ uint32_t screen_width_1;					// 0
	/* 160 */ uint32_t screen_height_1;					// 0
	/* 164 */ uint32_t screen_width_2;					// 0
	/* 168 */ uint32_t screen_height_2;					// 0
	/* 172 */ uint32_t screen_width_3;					// 0
	/* 176 */ uint32_t screen_height_3;                 // 0
	/* 180 */ uint32_t frame_rate;						// 24
	/* 184 */ uint32_t bit_rate;						// 0
	/* 188 */ uint32_t frame_interval;					// 4
	/* 192 */ uint32_t capture_frame_rate;				// 24
	/* 196 */ uint32_t capture_bit_rate;				// 0
	/* 200 */ uint32_t capture_frame_interval;			// 4
	/* 204 */ uint32_t in_out_app;						// 1
} __attribute__((packed)) screen_header_t;

#pragma mark - Internal methods definitions

void screen_header_response_init(
    screen_header_t * obj,
    uint32_t width,
    uint32_t height,
    uint32_t frame_rate,
    uint32_t frame_interval
);

#endif /* _SCREEN_HEADER_H_ */
