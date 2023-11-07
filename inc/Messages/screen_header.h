#ifndef _SCREEN_HEADER_H_
#define _SCREEN_HEADER_H_

#include "common_header.h"

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ uint32_t capture_width;
	/* 068 */ uint32_t capture_height;
	/* 072 */ uint32_t pixel_format;
	/* 076 */ uint32_t pixel_size;
	/* 080 */ uint32_t encoding_type;
	/* 084 */ uint32_t screen_orientation;
	/* 088 */ uint32_t screen_direct;
	/* 092 */ uint32_t capture_data_len;
	/* 096 */ uint32_t no;
	/* 100 */ uint32_t screen_width;
	/* 104 */ uint32_t screen_height;
	/* 108 */ uint32_t screen_pixel_format;
	/* 112 */ uint32_t screen_pixel_size;
	/* 116 */ uint32_t screen_capture_width;
	/* 120 */ uint32_t screen_capture_height;
	/* 124 */ uint32_t screen_capture_pixel_format;
	/* 128 */ uint32_t screen_captire_pixel_size;
	/* 132 */ uint32_t screen_capture_encoding_type;
	/* 136 */ uint32_t screen_capture_orientation;
	/* 140 */ uint32_t screen_capture_direct;
	/* 144 */ uint32_t screen_capture_number_of_screens;
	/* 148 */ uint32_t screen_width_0;
	/* 152 */ uint32_t screen_height_0;
	/* 156 */ uint32_t screen_width_1;
	/* 160 */ uint32_t screen_height_1;
	/* 164 */ uint32_t screen_width_2;
	/* 168 */ uint32_t screen_height_2;
	/* 172 */ uint32_t screen_width_3;
	/* 176 */ uint32_t screen_height_3;
	/* 180 */ uint32_t frame_rate;
	/* 184 */ uint32_t bit_rate;
	/* 188 */ uint32_t frame_interval;
	/* 192 */ uint32_t capture_frame_rate;
	/* 196 */ uint32_t capture_bit_rate;
	/* 200 */ uint32_t capture_frame_interval;
	/* 204 */ uint32_t in_out_app;
} __attribute__((packed)) screen_header_t;

#endif /* _SCREEN_HEADER_H_ */
