#ifndef _VERSION_H_
#define _VERSION_H_

#include "common_header.h"
#include "common_comand_header.h"

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ common_comand_header_t common_command_header;
	/* 072 */ uint32_t value;
	/* 076 */ uint8_t car_version[32];
	/* 104 */ uint8_t car_type[16];
	/* 120 */ uint32_t is_sleep;
	/* 124 */ uint32_t is_old_ssp_ver;
	/* 128 */ uint32_t car_width;
	/* 132 */ uint32_t car_height;
	/* 134 */ uint8_t unk0[58];
	/* 192 */ uint32_t ret;
} __attribute__((packed)) version_t;

#endif /* _VERSION_H_ */
