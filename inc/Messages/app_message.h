
#ifndef _APP_MESSAGE_H_
#define _APP_MESSAGE_H_

#include "common_header.h"

typedef struct {
	/* 00 */ common_header_t common_header;
	/* 64 */ uint32_t data_size;
	/* 68 */ uint32_t response_status;
} __attribute__((packed)) app_message_t;

#endif /* _APP_MESSAGE_H_ */
