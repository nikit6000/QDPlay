#ifndef _MESSAGE_COMMON_HEADER_V2_H_
#define _MESSAGE_COMMON_HEADER_V2_H_

#include <stdint.h>
#include <stdlib.h>

#define MESSAGE_COMMON_HEADER_V2_MARK                       "5A5A"
#define MESSAGE_COMMON_HEADER_V2_MARK_LEN                   (4)

#define MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_BIN            (0x00)
#define MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON           (0x01)
#define MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_VIDEO          (0x02)

#define MESSAGE_COMMON_HEADER_V2_SOURCE_PHONE               (0x00)
#define MESSAGE_COMMON_HEADER_V2_SOURCE_CAR                 (0x01)

#define MESSAGE_COMMON_HEADER_V2_MSG_TYPE_VIDEO             (0x01)
#define MESSAGE_COMMON_HEADER_V2_MSG_TYPE_TOUCH             (0x02)

#define MESSAGE_V2_JSON_CMD_KEY                             "CMD"
#define MESSAGE_V2_JSON_PARAMETERS_KEY                      "PARA"
#define MESSAGE_V2_JSON_CMD_HEARTBEAT                       "HEARTBEAT"
#define MESSAGE_V2_JSON_CMD_PHONE_INFO                      "PHONE_INFO"
#define MESSAGE_V2_JSON_CMD_CAR_INFO                        "CAR_INFO"
#define MESSAGE_V2_JSON_CMD_VIDEO_SUP_REQ                   "VIDEO_SUP_REQ"
#define MESSAGE_V2_JSON_CMD_VIDEO_SUP_RSP                   "VIDEO_SUP_RSP"
#define MESSAGE_V2_JSON_CMD_KEY_FRAME_REQ                   "KEY_FRAME_REQ"
#define MESSAGE_V2_JSON_CMD_VIDEO_CTRL                      "VIDEO_CTRL"
#define MESSAGE_V2_JSON_CMD_UPDATE_NOTIFY                   "UPDATE_NOTIFY"

typedef struct {
	/* 00 */ char mark[MESSAGE_COMMON_HEADER_V2_MARK_LEN];
    /* 04 */ uint32_t full_message_len;
    /* 08 */ uint16_t extended_header_size;
    /* 10 */ uint8_t msg_type;
    /* 11 */ uint8_t source;
    /* 12 */ uint8_t destination;
    /* 13 */ uint8_t payload_format;
    /* 14 */ uint8_t reserved;
    /* 15 */ uint8_t unk0;
} __attribute__((packed)) message_common_header_v2_t;

#endif
