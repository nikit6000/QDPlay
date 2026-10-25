#include "hu_message.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "utils/crc16.h"
#include "utils/common_utils.h"

#pragma mark - Private definitions

#define HU_MESSAGE_PACKET_MARK                                          "A6A6"
#define HU_MESSAGE_PACKET_MARK_LEN                                      (4)
#define HU_MESSAGE_ASSUME_NUM_BUFF_LEN                                  (32)

#define HU_MESSAGE_ASSUME_NON_NULL(x)                                   do {     \
    if (!x) {                                                                    \
        return NULL;                                                             \
    }                                                                            \
} while(0)

#define HU_MESSAGE_PACKET_PARSE_HEX_INT(dest, type, cur, buff)          do {     \
    int srt_num_len = sizeof(type) << 1;                                         \
    if (sizeof(buff) < srt_num_len + 1) {                                        \
        if (message) {                                                           \
            hu_message_free(message);                                            \
        }                                                                        \
        return NULL;                                                             \
    }                                                                            \
    memset((void*)buff, 0, sizeof(buff));                                        \
    memcpy((void*)buff, cur, srt_num_len);                                       \
    dest = (type)strtol((char*)buff, NULL, 16);                                  \
    cur += srt_num_len;                                                          \
} while(0) 

#define HU_MESSAGE_PACKET_PARSE_STR(dest, len, cur)                     do {     \
    dest = (char*)malloc(len + 1);                                               \
    if (!dest) {                                                                 \
        if (message) {                                                           \
            hu_message_free(message);                                            \
        }                                                                        \
        return NULL;                                                             \
    }                                                                            \
    memset((void*)dest, 0, len + 1);                                             \
    memcpy((void*)dest, cur, len);                                               \
    cur += len;                                                                  \
} while(0);

#define HU_MESSAGE_PACKET_PUT_INT(cur, x, len)                          do {     \
    common_utils_dec_to_hex((char*)cur, len, x);                                 \
    cur += len;                                                                  \
} while(0)

#define HU_MESSAGE_PACKET_PUT_STR(cur, x, len)                          do {     \
    int str_len = (int)strlen(x);                                                \
    HU_MESSAGE_PACKET_PUT_INT(cur, str_len, len);                                \
    memcpy(cur, x, str_len);                                                     \
    cur += str_len;                                                              \
} while(0) 

#pragma mark - Private methods definition

uint32_t hu_message_calc_len(const hu_message_t* message);

#pragma mark - Internal methods implementation

hu_message_t* hu_message_init(const char * app_id, const char * logic_id, uint8_t data_count, ...) {
    hu_message_t* message = (hu_message_t*)malloc(sizeof(hu_message_t));
    
    if (!message) {
        return NULL;
    }
    
    memset(message, 0, sizeof(hu_message_t));
    message->app_id = (char*)malloc(strlen(app_id) + 1);
    message->logic_id = (char*)malloc(strlen(logic_id) + 1);
    message->data = (hu_message_data_t*)malloc(sizeof(hu_message_data_t*) * data_count);
    
    if (!message->app_id || !message->logic_id || (!message->data && data_count > 0)) {
        hu_message_free(message);
        
        return NULL;
    }
    
    strcpy(message->app_id, app_id);
    strcpy(message->logic_id, logic_id);
    
    va_list arguments;
     
    va_start(arguments, data_count);
    
    for (uint8_t i = 0; i < data_count; i++) {
        const char * arg = va_arg(arguments, const char *);
        
        message->data[i] = (hu_message_data_t)malloc(strlen(arg) + 1);
        
        if (message->data[i] == NULL) {
            hu_message_free(message);
            va_end(arguments);
            
            return NULL;
        }
        
        strcpy(message->data[i], arg);
        
        message->data_count++;
    }
    
    va_end(arguments);
    
    return message;
}

// Reads `digits` hex characters. Returns FALSE if the buffer is too short or the text is not hex.
static int hu_message_read_hex(const uint8_t** cursor, const uint8_t* end, int digits, uint32_t* out) {
    char number_buffer[HU_MESSAGE_ASSUME_NUM_BUFF_LEN];
    char* parse_end = NULL;

    if (end - *cursor < digits || digits >= HU_MESSAGE_ASSUME_NUM_BUFF_LEN) {
        return 0;
    }

    memcpy(number_buffer, *cursor, digits);
    number_buffer[digits] = '\0';

    *out = (uint32_t)strtoul(number_buffer, &parse_end, 16);
    *cursor += digits;

    return parse_end == number_buffer + digits;
}

static char* hu_message_read_str(const uint8_t** cursor, const uint8_t* end, uint32_t len) {
    char* result = NULL;

    if ((uint32_t)(end - *cursor) < len) {
        return NULL;
    }

    result = (char*)malloc(len + 1);

    if (!result) {
        return NULL;
    }

    memcpy(result, *cursor, len);
    result[len] = '\0';
    *cursor += len;

    return result;
}

hu_message_t* hu_message_decode(const uint8_t* data, uint32_t len) {
    const uint8_t* cursor = data;
    const uint8_t* end = data + len;
    uint32_t value = 0;
    uint32_t total_len = 0;
    uint32_t total_data_count = 0;
    hu_message_t* message = NULL;

    HU_MESSAGE_ASSUME_NON_NULL(data);

    if (len < HU_MESSAGE_PACKET_MARK_LEN || memcmp(data, HU_MESSAGE_PACKET_MARK, HU_MESSAGE_PACKET_MARK_LEN) != 0) {
        return NULL;
    }

    cursor += HU_MESSAGE_PACKET_MARK_LEN;

    message = (hu_message_t*)calloc(1, sizeof(hu_message_t));
    HU_MESSAGE_ASSUME_NON_NULL(message);

    if (!hu_message_read_hex(&cursor, end, 2, &value)) goto error;
    message->flow_id = (uint8_t)value;

    if (!hu_message_read_hex(&cursor, end, 8, &total_len) || total_len > len) goto error;

    // Everything below must stay inside the declared message
    end = data + total_len;

    if (!hu_message_read_hex(&cursor, end, 2, &value)) goto error;
    if (!(message->app_id = hu_message_read_str(&cursor, end, value))) goto error;

    if (!hu_message_read_hex(&cursor, end, 2, &value)) goto error;
    if (!(message->logic_id = hu_message_read_str(&cursor, end, value))) goto error;

    if (!hu_message_read_hex(&cursor, end, 2, &total_data_count)) goto error;

    if (total_data_count == 0) {
        return message;
    }

    message->data = (hu_message_data_t*)calloc(total_data_count, sizeof(hu_message_data_t));

    if (!message->data) goto error;

    for (uint32_t i = 0; i < total_data_count; i++) {
        if (!hu_message_read_hex(&cursor, end, 8, &value)) goto error;
        if (!(message->data[i] = hu_message_read_str(&cursor, end, value))) goto error;

        message->data_count++;
    }

    return message;

error:
    hu_message_free(message);

    return NULL;
}

uint8_t* hu_message_encode(const hu_message_t* message, uint16_t chunk_len, uint32_t* out_len) {
    HU_MESSAGE_ASSUME_NON_NULL(message);

    uint32_t total_size = hu_message_calc_len(message);
    uint32_t chunked_total_size = 0;

    if (out_len) {
        *out_len = total_size;
    }

    if (chunk_len > 0) {
        chunked_total_size = (total_size / chunk_len) * chunk_len;

        if (chunked_total_size < total_size) {
            chunked_total_size += chunk_len;
        }
    } else {
        chunked_total_size = total_size;
    }

    uint8_t* data = (uint8_t*)malloc(chunked_total_size);
    uint8_t* cursor = data;
    HU_MESSAGE_ASSUME_NON_NULL(data);
    
    memcpy(cursor, HU_MESSAGE_PACKET_MARK, HU_MESSAGE_PACKET_MARK_LEN);
    cursor += HU_MESSAGE_PACKET_MARK_LEN;

    HU_MESSAGE_PACKET_PUT_INT(cursor, message->flow_id, 2);
    HU_MESSAGE_PACKET_PUT_INT(cursor, chunked_total_size, 8);
    HU_MESSAGE_PACKET_PUT_STR(cursor, message->app_id, 2);
    HU_MESSAGE_PACKET_PUT_STR(cursor, message->logic_id, 2);
    HU_MESSAGE_PACKET_PUT_INT(cursor, message->data_count, 2);

    for (int i = 0; i < message->data_count; i++) {
        HU_MESSAGE_PACKET_PUT_STR(cursor, message->data[i], 8);
    }

    uint16_t crc = crc16_xmodem(data, total_size - 4);
    HU_MESSAGE_PACKET_PUT_INT(cursor, crc, 4);

    return data;
}

void hu_message_free(const hu_message_t* message) {
    if (!message) {
        return;
    }

    if (message->app_id) {
        free((void*)message->app_id);
    }
    
    if (message->logic_id) {
        free((void*)message->logic_id);
    }

    if (message->data) {
        for (int i = 0; i < message->data_count; i++) {
            free(message->data[i]);
        }
        free(message->data);
    }

    free((void*)message);
}

#pragma mark - Private methods implementations

uint32_t hu_message_calc_len(const hu_message_t* message) {
    if (!message) {
        return 0;
    }

    uint32_t total_size = 0;

    total_size += HU_MESSAGE_PACKET_MARK_LEN;                 // MARK
    total_size += sizeof(message->flow_id) << 1;              // FLOW_ID
    total_size += sizeof(uint32_t) << 1;                      // TOTAL_LEN
    total_size += sizeof(uint8_t) << 1;                       // APP_ID_LEN

    if (message->app_id) {
        total_size += strlen(message->app_id);                // APP_ID
    }
    
    total_size += sizeof(uint8_t) << 1;                       // LOGIC_ID_LEN
    
    if (message->logic_id) {
        total_size += strlen(message->logic_id);              // LOGIC_ID
    }
    
    total_size += sizeof(uint8_t) << 1;                       // DATA_COUNT

    for (int i = 0; i < message->data_count; i++) {
        total_size += sizeof(uint32_t) << 1;                  // DATA_LEN
        total_size += strlen(message->data[i]);               // DATA
    }

    total_size += sizeof(uint16_t) << 1;                       // CRC

    return total_size;
}
