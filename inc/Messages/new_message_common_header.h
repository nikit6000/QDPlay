#ifndef _NEW_MESSAGE_COMMON_HEADER_H_
#define _NEW_MESSAGE_COMMON_HEADER_H_

#include <stdint.h>
#include <stdlib.h>
#include <macros/data_types.h>

typedef struct {
	/* 00 */ char mark[MESSAGE_NEW_HEADER_LEN];
    /* 04 */ uint32_t full_message_len;
    /* 08 */ uint16_t extended_header_size;
    /* 10 */ uint8_t msg_type;
    /* 11 */ uint8_t source;
    /* 12 */ uint8_t destination;
    /* 13 */ uint8_t payload_format;
    /* 14 */ uint8_t reserved;
    /* 15 */ uint8_t unk0;
} __attribute__((packed)) new_message_common_header_t;

#pragma mark - Internal function definitions

int new_message_heartbeat_init(uint8_t* buffer, size_t len); 

#endif
