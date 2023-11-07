#ifndef _APP_STATUS_H_
#define _APP_STATUS_H_

#include "common_header.h"
#include "common_comand_header.h"

typedef struct {
	/* 00 */ common_header_t common_header;
	/* 64 */ common_comand_header_t common_command_header;
	/* 72 */ uint32_t value;
	/* 76 */ uint32_t android_version;
	/* 80 */ uint32_t integrator_server;
	/* 84 */ uint32_t ret;
} __attribute__((packed)) app_status_t;

#endif /* _APP_STATUS_H_ */
