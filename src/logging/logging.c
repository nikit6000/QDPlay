#include "logging/logging.h"

#pragma mark - Open function implementations

void logger_log(logging_level_t level, const gchar *tag, const gchar *format, ...)
{

	va_list args;
	va_start(args, format);

	const gchar *log_tag = NULL;

	switch (level)
	{
	case LOGGING_LEVEL_INFO:
		log_tag = "INFO";
		break;

	case LOGGING_LEVEL_WARN:
		log_tag = "WARNING";
		break;

	case LOGGING_LEVEL_ERR:
		log_tag = "ERROR";
		break;

	default:
		log_tag = "UNKNOWN";
		break;
	}

	const gchar *full_format = g_strdup_printf("[%s][%s]: %s\n", log_tag, tag, format);
	const gchar *formatted = g_strdup_vprintf(full_format, args);

	va_end(args);

	g_print(formatted);

	g_free((void *)full_format);
	g_free((void *)formatted);
}