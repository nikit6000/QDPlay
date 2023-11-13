#ifndef _LAND_MODE_H_
#define _LAND_MODE_H_

#include "common_header.h"
#include "common_comand_header.h"

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ common_comand_header_t common_command_header;
	/* 072 */ uint32_t value;
	/* 076 */ uint8_t unk0[116];
	/* 192 */ uint32_t ret;
	/* 196 */ uint32_t is_value;
} __attribute__((packed)) land_mode_t;

#pragma mark - Interal methods definitions

void land_mode_response_init(land_mode_t * obj);

#endif /* _LAND_MODE_H_ */
