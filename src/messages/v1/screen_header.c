#include <string.h>
#include <netinet/in.h>
#include <glib.h>
#include <byteswap.h>
#include "messages/v1/screen_header.h"
#include "macros/data_types.h"
#include "macros/comand_types.h"

#pragma mark - Internal methods implementations

gboolean qd_screen_header_h264_response_init(
	screen_header_t * obj,
	uint32_t width,
	uint32_t height,
	uint32_t frame_rate,
	uint32_t frame_interval
) {
	if (!obj) {
		return FALSE;
	}

	memcpy(obj->common_header.binary_mark, MESSAGE_BINARY_HEADER, MESSAGE_BINARY_HEADER_LEN);
	obj->common_header.data_type = bswap_32(MESSAGE_DATA_TYPE_SCREEN_CAPTURE);
	obj->common_header.header_size = bswap_32(512);
	obj->common_header.common_header_size = bswap_32(64);
	obj->common_header.request_header_size = bswap_32(128);
	obj->common_header.response_header_size = bswap_32(128);
	obj->common_header.action = bswap_32(1);

	obj->capture_width = bswap_32(width);
	obj->capture_height = bswap_32(height);
	obj->pixel_format = bswap_32(4);
	obj->pixel_size = bswap_32(2);
	obj->encoding_type = bswap_32(SCREEN_HEADER_VIDEO_FORMAT_H264);
	obj->screen_orientation = bswap_32(0);
	obj->screen_direct = bswap_32(1);
	obj->no = bswap_32(0);
	obj->screen_width = bswap_32(width);
	obj->screen_height = bswap_32(height);
	obj->screen_pixel_format = bswap_32(0);
	obj->screen_pixel_size = bswap_32(0);
	obj->screen_capture_width = bswap_32(width);
	obj->screen_capture_height = bswap_32(height);
	obj->screen_capture_pixel_format = bswap_32(0);
	obj->screen_captire_pixel_size = bswap_32(0);
	obj->screen_capture_encoding_type = bswap_32(SCREEN_HEADER_VIDEO_FORMAT_H264);
	obj->screen_capture_orientation = bswap_32(90);
	obj->screen_capture_direct = bswap_32(90);
	obj->screen_capture_number_of_screens = bswap_32(0);
	obj->screen_width_0 = bswap_32(width);
	obj->screen_height_0 = bswap_32(height);
	obj->screen_width_1 = bswap_32(0);
	obj->screen_height_1 = bswap_32(0);
	obj->screen_width_2 = bswap_32(0);
	obj->screen_height_2 = bswap_32(0);
	obj->screen_width_3 = bswap_32(0);
	obj->screen_height_3 = bswap_32(0);
	obj->frame_rate = bswap_32(frame_rate);
	obj->bit_rate = bswap_32(0);
	obj->frame_interval = bswap_32(frame_interval);
	obj->capture_frame_rate = bswap_32(frame_rate);
	obj->capture_bit_rate = bswap_32(0);
	obj->capture_frame_interval = bswap_32(frame_interval);
	obj->in_out_app = bswap_32(1);

	for (int i = 0; i < 32; i++)
	{
		obj->common_header.mark[i] = i + 32;
	}

	return TRUE;
}