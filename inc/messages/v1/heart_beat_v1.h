#ifndef _HEART_BEAT_V1_H_
#define _HEART_BEAT_V1_H_

#include "common_header.h"
#include "common_comand_header.h"

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ common_comand_header_t common_command_header;
	/* 072 */ uint32_t value;
	/* 076 */ uint8_t unk0[116];
	/* 192 */ uint32_t ret;
} __attribute__((packed)) heart_beat_v1_t;

#pragma mark - Internal methods definition 

void heart_beat_v1_init(heart_beat_v1_t* obj);

#endif /* _SPEECH_STATUS_H_ */
