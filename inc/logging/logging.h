#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <glib.h>

typedef enum {
    LOGGING_LEVEL_INFO = 0,
    LOGGING_LEVEL_WARN,
    LOGGING_LEVEL_ERR
} logging_level_t;

#ifndef ENABLE_LOGGING

#define LOG_I(tag, format, ...)                    do {                             \
    logger_log(LOGGING_LEVEL_INFO, tag, format __VA_OPT__(,) __VA_ARGS__);         \
} while(0)                                                             

#define LOG_W(tag, format, ...)                    do {                             \
    logger_log(LOGGING_LEVEL_WARN, tag, format __VA_OPT__(,) __VA_ARGS__);         \
} while(0)   

#define LOG_E(tag, format, ...)                    do {                             \
    logger_log(LOGGING_LEVEL_ERR, tag, format __VA_OPT__(,) __VA_ARGS__);          \
} while(0)

#else

#define LOG_I(tag, format, ...)                    do { } while(0)
#define LOG_W(tag, format, ...)                    do { } while(0)
#define LOG_E(tag, format, ...)                    do { } while(0)

#endif

#pragma mark - Open functions prototypes

void logger_log(logging_level_t level, const gchar* tag, const gchar* format, ...);

#endif
