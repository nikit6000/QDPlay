#ifndef _HU_MESSAGE_H_
#define _HU_MESSAGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Types

typedef char* hu_message_data_t;

typedef struct {
	uint8_t flow_id;
	char * app_id;
	char * logic_id;
	uint8_t data_count;
	hu_message_data_t * data;
} hu_message_t;

#pragma mark - Internal methods definition

hu_message_t* hu_message_init(const char * app_id, const char * logic_id, uint8_t data_count, ...);
hu_message_t* hu_message_decode(const uint8_t* data, uint32_t len);
uint8_t* hu_message_encode(const hu_message_t* message, uint16_t chunk_len, uint32_t* out_len);
void hu_message_free(const hu_message_t* message);

#ifdef __cplusplus
}
#endif
#endif
