#ifndef _DATA_TYPES_H_
#define _DATA_TYPES_H_

#define MESSAGE_BINARY_HEADER                       "!BIN"
#define MESSAGE_NEW_HEADER                          "5A5A"
#define MESSAGE_BINARY_HEADER_LEN                   4
#define MESSAGE_NEW_HEADER_LEN                      4

#define MESSAGE_DATA_TYPE_CMD						0
#define MESSAGE_DATA_TYPE_SCREEN_CAPTURE			1
#define MESSAGE_DATA_TYPE_KEY_EVENT					2
#define MESSAGE_DATA_TYPE_HU_MSG					3
#define MESSAGE_DATA_TYPE_UPGRADE					5
#define MESSAGE_DATA_TYPE_IN_APP_CONTROL			10
#define MESSAGE_DATA_TYPE_SPEECH_STATUS				12
#define MESSAGE_DATA_TYPE_APP_DATA					13
#define MESSAGE_DATA_TYPE_CUSTOM_STATUS				99

#define MESSAGE_DEFAULT_CHUNK_SIZE                  512
#define MESSAGE_DEFAULT_CHUNK_LOG2                  9

#endif /* _DATA_TYPES_H_ */
