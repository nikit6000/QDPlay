#ifndef _MESSAGING_SERVICE_H_
#define _MESSAGING_SERVICE_H_

#include <stdint.h>
#include <sys/types.h>
#include <glib.h>
#include "messaging_service_parcels.h"

#pragma mark - Internal methods

gboolean messaging_service_init(void);
void messaging_service_send_key_frame_req(void);
void messaging_service_send_input_event(messaging_service_input_event_t event);
void messaging_service_notify_sources(pid_t old_active_pid, pid_t new_active_pid, size_t sources_count);
void messaging_service_set_screen_size(uint16_t width, uint16_t height);

#endif
