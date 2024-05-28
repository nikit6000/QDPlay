#ifndef _MESSAGING_SERVICE_H_
#define _MESSAGING_SERVICE_H_

#include <stdint.h>
#include "messaging_service_parcels.h"

#pragma mark - Internal methods

gboolean messaging_service_init(void);
void messaging_service_send_key_frame_req(void);
void messaging_service_send_input_event(messaging_service_input_event_t event);

#endif
