#include <string.h>
#include <netinet/in.h>
#include "messages/speech.h"
#include "macros/data_types.h"

#pragma mark - Internal methods implementations

void speech_response_init(speech_t * obj) {
    if (!obj) {
        return;
    }

    memcpy(obj->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN);
    obj->common_header.data_type = ntohl(12);
    obj->common_header.header_size = ntohl(512);
    obj->common_header.total_size = ntohl(512);
    obj->common_header.common_header_size = ntohl(64);
    obj->common_header.request_header_size = ntohl(16);
    obj->common_header.response_header_size = ntohl(16);
    obj->common_header.action = ntohl(0);
    
    obj->encoding_type = ntohl(1);
    obj->sample_rate = ntohl(16000);
    obj->channel_config = ntohl(1);
    obj->audio_format = ntohl(16);

    for (int i = 0; i < 32; i++) {
        obj->common_header.mark[i] = i + 32;
    }
}