#ifndef _MESSAGE_CMD_RESPONSE_V2_H_
#define _MESSAGE_CMD_RESPONSE_V2_H_

#include <stdint.h>
#include <stdlib.h>

#pragma mark - Internal types

typedef struct {
    char cmd[256];
} message_cmd_response_v2_t;

#pragma mark - Internal methods definition

int message_cmd_response_v2_init(const char* buffer, size_t len, message_cmd_response_v2_t *obj);

#endif
