#ifndef _MIRROR_SUPPORT_H_
#define _MIRROR_SUPPORT_H_

#include "common_header.h"
#include "common_comand_header.h"

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ common_comand_header_t common_command_header;
	/* 072 */ uint32_t screen_capture_support;
	/* 076 */ uint8_t unk0[116];
	/* 192 */ uint32_t ret;
} __attribute__((packed)) mirror_support_t;

#pragma mark - Interal methods definitions

void mirror_support_response_init(mirror_support_t * obj);

#endif /* _MIRROR_SUPPORT_H_ */
