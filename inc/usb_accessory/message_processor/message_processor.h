#ifndef _MESSAGE_PROCESSOR_H_
#define _MESSAGE_PROCESSOR_H_

#include <glib.h>
#include <stdint.h> 
#include <pthread.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
} message_processor_video_params_t;

typedef gboolean (*message_processor_probe)(uint8_t * buffer, size_t len);
typedef gboolean (*message_processor_run)(void);
typedef gboolean (*message_processor_write_hb)(void);
typedef int (*message_processor_write_h264)(message_processor_video_params_t video_parameters, void * buffer, size_t len);

typedef struct
{
    message_processor_probe probe;
    message_processor_run run;
    message_processor_write_hb write_hb;
    message_processor_write_h264 write_h264;
} message_processor_t;

#endif
