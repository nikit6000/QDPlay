#ifndef _CRC16_H_
#define _CRC16_H_

#include <stdlib.h>
#include <stdint.h>

uint16_t crc16_xmodem(const void *buf, size_t len);

#endif
