#ifndef _COMMON_COMAND_HEADER_H_
#define _COMMON_COMAND_HEADER_H_

#include <stdint.h>

typedef struct {
	uint32_t time_stamp;
	uint32_t cmd;
} __attribute__((packed)) common_comand_header_t;

#endif /* _COMMON_COMAND_HEADER_H_ */
