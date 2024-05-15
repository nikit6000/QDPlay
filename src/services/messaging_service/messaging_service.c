#include <pthread.h>
#include <glib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <byteswap.h>
#include "services/messaging_service/messaging_service.h"

#pragma mark - Private definitiosn

#define MESSAGING_SERVICE_SOCKET_PATH                      "/tmp/qdplay.messaging.socket"
#define MESSAGING_SERVICE_MAX_CLIENTS                      (5)

#pragma mark - Private types

typedef struct {
    int fd;
} messaging_service_client_t;

typedef struct {
    int server_fd;
    pthread_t clients_handler_thread_id;
    pthread_mutex_t mutex;
    pthread_mutex_t broadcast_mutex;
    messaging_service_client_t clients[MESSAGING_SERVICE_MAX_CLIENTS];
    size_t number_of_clients;
} messaging_service_state_t;

#pragma mark - Private propertioes

messaging_service_state_t messaging_service_state = {
    .server_fd = -1,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .broadcast_mutex = PTHREAD_MUTEX_INITIALIZER,
    .number_of_clients = 0
};

#pragma mark - Private methods definition

void messaging_service_broadcast(uint8_t * data, size_t len);
void * messaging_service_handle_clients(void * context);

#pragma mark - Internal methods

gboolean messaging_service_init(void) {
    struct sockaddr_un server_addr;

    pthread_mutex_lock(&messaging_service_state.mutex);

    if (messaging_service_state.server_fd > 0) {
        pthread_mutex_unlock(&messaging_service_state.mutex);

        return FALSE;
    }

    messaging_service_state.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (messaging_service_state.server_fd < 0) {
        pthread_mutex_unlock(&messaging_service_state.mutex);
        
        return FALSE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sun_family = AF_UNIX;

    strcpy(server_addr.sun_path, MESSAGING_SERVICE_SOCKET_PATH);

    unlink(MESSAGING_SERVICE_SOCKET_PATH);

    if (bind(messaging_service_state.server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(messaging_service_state.server_fd);

        messaging_service_state.server_fd = -1;

        pthread_mutex_unlock(&messaging_service_state.mutex);

        return FALSE;
    }

	if (listen(messaging_service_state.server_fd, MESSAGING_SERVICE_MAX_CLIENTS) < 0) {
		close(messaging_service_state.server_fd);

        messaging_service_state.server_fd = -1;

        pthread_mutex_unlock(&messaging_service_state.mutex);

        return FALSE;
	}

	pthread_create(
		&messaging_service_state.clients_handler_thread_id, 
		NULL,
		messaging_service_handle_clients,
		NULL
	);

    pthread_mutex_unlock(&messaging_service_state.mutex);

    return TRUE;
}

void messaging_service_send_key_frame_req(void) {
    messaging_service_parcel_header_t message = {
        .mark = bswap_32(MESSAGING_SERVICE_MARK),
        .event_id = bswap_32(MESSAGING_SERVICE_KEY_FRAME_REQ_EVENT),
        .full_len = bswap_32(sizeof(messaging_service_parcel_header_t)),
        .payload_len = 0
    };

    messaging_service_broadcast((uint8_t*)&message, sizeof(messaging_service_parcel_input_t));
}

void messaging_service_send_input_event(messaging_service_input_event_t event) {
    messaging_service_parcel_input_t message = {
        .header = {
            .mark = bswap_32(MESSAGING_SERVICE_MARK),
            .event_id = bswap_32(MESSAGING_SERVICE_INPUT_EVENT),
            .full_len = bswap_32(sizeof(messaging_service_parcel_input_t)),
            .payload_len = bswap_32(sizeof(messaging_service_input_event_t))
        },
        .event = {
            .action = bswap_16(event.action),
            .x = bswap_16(event.x),
            .y = bswap_16(event.y)
        }
    };

    messaging_service_broadcast((uint8_t*)&message, sizeof(messaging_service_parcel_input_t));
}

#pragma mark - Private methods

void messaging_service_broadcast(uint8_t * data, size_t len) {
    pthread_mutex_lock(&messaging_service_state.broadcast_mutex);

    for (size_t client_index = 0; client_index < messaging_service_state.number_of_clients; client_index++) {
        int client_fd = messaging_service_state.clients[client_index].fd;

        if (send(client_fd, data, len, 0) > 0) {
            continue;
        }

        close(client_fd);

        messaging_service_state.clients[client_index--] = messaging_service_state.clients[--messaging_service_state.number_of_clients];
    }

    pthread_mutex_unlock(&messaging_service_state.broadcast_mutex);
}

void * messaging_service_handle_clients(void * context) {
    
    while (1) {
        int client_fd = accept(messaging_service_state.server_fd, NULL, NULL);

        if (client_fd < 0) {
            continue;
        } 

        pthread_mutex_lock(&messaging_service_state.broadcast_mutex);

        if (messaging_service_state.number_of_clients < MESSAGING_SERVICE_MAX_CLIENTS) {
            messaging_service_state.clients[messaging_service_state.number_of_clients].fd = client_fd;
            messaging_service_state.number_of_clients += 1;
        }

        pthread_mutex_unlock(&messaging_service_state.broadcast_mutex);
    }

    return NULL;
}
