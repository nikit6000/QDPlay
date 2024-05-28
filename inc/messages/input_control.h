#ifndef _INPUT_CONTROL_H_
#define _INPUT_CONTROL_H_

#include "common_header.h"

typedef struct {
	/* 00 */ common_header_t common_header;
	/* 64 */ int16_t event_number;
	/* 66 */ int16_t event_type; // Short.MIN_VALUE
	/* 68 */ int16_t event_value0; // 511
	/* 70 */ int16_t event_value1; // 1023
	/* 72 */ int16_t event_value2; // 511
	/* 74 */ int16_t event_value3; // 0
	/* 76 */ int16_t type; // -32766
	/* 78 */ int16_t x; // 799
	/* 80 */ int16_t y; // 512
	/* 82 */ int16_t press; // 127
	/* 84 */ int16_t finger; // 0
} __attribute__((packed)) input_control_t;

#endif /* _INPUT_CONTROL_H_ */
