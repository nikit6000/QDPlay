#include "utils/common_utils.h"

#include <string.h>

#pragma mark - Internal methods implementations

void common_utils_dec_to_hex(char* dst, size_t len, uint32_t value) {
	memset(dst, '0', len);

	const char letters[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
	
	while ((value > 0) && (len > 0)) {
		dst[--len] = letters[value % 16];
		value = value / 16;
	}
}