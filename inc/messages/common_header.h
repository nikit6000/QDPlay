#ifndef _COMMON_HEADER_H_
#define _COMMON_HEADER_H_

#include <stdint.h>
#include <macros/data_types.h>

typedef struct {
	/* 00 */ uint8_t binary_mark[MESSAGE_BINARY_HEADER_LEN];
	/* 04 */ uint32_t data_type;
	/* 08 */ uint32_t total_size;
	/* 12 */ uint32_t header_size;
	/* 16 */ uint32_t common_header_size;
	/* 20 */ uint32_t request_header_size;
	/* 24 */ uint32_t response_header_size;
	/* 28 */ uint32_t action;
	/* 32 */ uint8_t mark[32];
} __attribute__((packed)) common_header_t;

#endif /* _COMMON_HEADER_H_ */
