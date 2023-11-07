#ifndef _SPEECH_H_
#define _SPEECH_H_

#include "common_header.h"

typedef struct {
	/* 000 */ common_header_t common_header;
	/* 064 */ uint32_t encoding_type;
	/* 068 */ uint32_t sample_rate;
	/* 072 */ uint32_t channel_config;
	/* 076 */ uint32_t audio_format;
	/* 080 */ uint32_t unk[208];
	/* 288 */ uint32_t no;
	/* 292 */ uint32_t output_encoding_type;
	/* 296 */ uint32_t output_sample_rate;
	/* 300 */ uint32_t output_channel_config;
	/* 304 */ uint32_t output_audio_format;
} __attribute__((packed)) speech_t;

#endif /* _SPEECH_H_ */
