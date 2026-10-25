#define _GNU_SOURCE
#include <pthread.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <byteswap.h>
#include "logging/logging.h"
#include "services/messaging_service/messaging_service.h"
#include "services/video_receiver/video_receiver.h"

#pragma mark - Private definitiosn

#define MESSAGING_SERVICE_SOCKET_PATH                      "/tmp/qdplay.messaging.socket"
#define MESSAGING_SERVICE_MAX_CLIENTS                      (8)
#define MESSAGING_SERVICE_RX_BUFFER_SIZE                   (64)
#define MESSAGING_SERVICE_SND_BUFFER_SIZE                  (64 * 1024)

#pragma mark - Private types

typedef struct {
    int fd;
    pid_t pid;
    uint8_t rx_buffer[MESSAGING_SERVICE_RX_BUFFER_SIZE];
    size_t rx_filled;
} messaging_service_client_t;

typedef struct {
    int server_fd;
    pthread_t clients_handler_thread_id;
    pthread_mutex_t mutex;
    messaging_service_client_t clients[MESSAGING_SERVICE_MAX_CLIENTS];
    size_t number_of_clients;
} messaging_service_state_t;

typedef enum {
    MESSAGING_SERVICE_ROUTE_ALL = 0,
    // Deliver to the clients of the active video source. If that process has
    // no control connection (e.g. separate sender/receiver tools), deliver to everyone.
    MESSAGING_SERVICE_ROUTE_ACTIVE_OR_ALL,
    MESSAGING_SERVICE_ROUTE_PID_ONLY
} messaging_service_route_t;

#pragma mark - Private propertioes

static messaging_service_state_t messaging_service_state = {
    .server_fd = -1,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .number_of_clients = 0
};

static const gchar* messaging_service_tag = "MessagingService";

// Last resolution reported by the head unit, guarded by the state mutex
static uint16_t messaging_service_screen_width = 0;
static uint16_t messaging_service_screen_height = 0;

#pragma mark - Private methods definition

static void messaging_service_send(messaging_service_route_t route, pid_t pid, const uint8_t * data, size_t len);
static void * messaging_service_handle_clients(void * context);
static gboolean messaging_service_send_to_client(messaging_service_client_t *client, const uint8_t * data, size_t len);
static void messaging_service_fill_screen_info(messaging_service_parcel_screen_info_t *message, uint16_t width, uint16_t height);

#pragma mark - Internal methods

gboolean messaging_service_init(void) {
    struct sockaddr_un server_addr;

    pthread_mutex_lock(&messaging_service_state.mutex);

    if (messaging_service_state.server_fd >= 0) {
        pthread_mutex_unlock(&messaging_service_state.mutex);

        return FALSE;
    }

    messaging_service_state.server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (messaging_service_state.server_fd < 0) {
        pthread_mutex_unlock(&messaging_service_state.mutex);

        return FALSE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sun_family = AF_UNIX;

    strncpy(server_addr.sun_path, MESSAGING_SERVICE_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

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

	if (pthread_create(&messaging_service_state.clients_handler_thread_id, NULL, messaging_service_handle_clients, NULL) != 0) {
        close(messaging_service_state.server_fd);

        messaging_service_state.server_fd = -1;

        pthread_mutex_unlock(&messaging_service_state.mutex);

        return FALSE;
    }

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

    messaging_service_send(MESSAGING_SERVICE_ROUTE_ACTIVE_OR_ALL, video_receiver_active_pid(), (uint8_t*)&message, sizeof(message));
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

    messaging_service_send(MESSAGING_SERVICE_ROUTE_ACTIVE_OR_ALL, video_receiver_active_pid(), (uint8_t*)&message, sizeof(message));
}

void messaging_service_notify_sources(pid_t old_active_pid, pid_t new_active_pid, size_t sources_count) {
    messaging_service_parcel_active_state_t message = {
        .header = {
            .mark = bswap_32(MESSAGING_SERVICE_MARK),
            .event_id = bswap_32(MESSAGING_SERVICE_ACTIVE_STATE_EVENT),
            .full_len = bswap_32(sizeof(messaging_service_parcel_active_state_t)),
            .payload_len = bswap_32(sizeof(messaging_service_active_state_t))
        },
        .event = {
            .sources_count = sources_count > 255 ? 255 : (uint8_t)sources_count
        }
    };

    // Every client learns whether it is on screen and how many sources exist

    pthread_mutex_lock(&messaging_service_state.mutex);

    for (size_t i = 0; i < messaging_service_state.number_of_clients; i++) {
        messaging_service_client_t *client = &messaging_service_state.clients[i];

        message.event.active = (new_active_pid != 0 && client->pid == new_active_pid) ? 1 : 0;

        messaging_service_send_to_client(client, (uint8_t*)&message, sizeof(message));
    }

    pthread_mutex_unlock(&messaging_service_state.mutex);

    if (new_active_pid != 0 && new_active_pid != old_active_pid) {
        // The new source must start with an IDR frame, otherwise the head unit shows garbage
        messaging_service_send_key_frame_req();
    }
}

void messaging_service_set_screen_size(uint16_t width, uint16_t height) {
    messaging_service_parcel_screen_info_t message;

    pthread_mutex_lock(&messaging_service_state.mutex);
    messaging_service_screen_width = width;
    messaging_service_screen_height = height;
    pthread_mutex_unlock(&messaging_service_state.mutex);

    messaging_service_fill_screen_info(&message, width, height);
    messaging_service_send(MESSAGING_SERVICE_ROUTE_ALL, 0, (uint8_t*)&message, sizeof(message));
}

#pragma mark - Private methods

static void messaging_service_fill_screen_info(messaging_service_parcel_screen_info_t *message, uint16_t width, uint16_t height) {
    message->header.mark = bswap_32(MESSAGING_SERVICE_MARK);
    message->header.event_id = bswap_32(MESSAGING_SERVICE_SCREEN_INFO_EVENT);
    message->header.full_len = bswap_32(sizeof(messaging_service_parcel_screen_info_t));
    message->header.payload_len = bswap_32(sizeof(messaging_service_screen_info_t));
    message->event.width = bswap_16(width);
    message->event.height = bswap_16(height);
}

static gboolean messaging_service_send_to_client(messaging_service_client_t *client, const uint8_t * data, size_t len) {
    ssize_t sent;

    do {
        // Never block the USB processing thread because of a stuck client
        sent = send(client->fd, data, len, MSG_DONTWAIT | MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);

    if (sent == (ssize_t)len) {
        return TRUE;
    }

    // A partial message would break the stream framing, so drop the client.
    // The handler thread notices the shutdown and releases the slot.

    LOG_W(messaging_service_tag, "Client %d (pid %d) is not reading, dropping it", client->fd, client->pid);

    shutdown(client->fd, SHUT_RDWR);

    return FALSE;
}

static void messaging_service_send(messaging_service_route_t route, pid_t pid, const uint8_t * data, size_t len) {
    gboolean delivered = FALSE;

    pthread_mutex_lock(&messaging_service_state.mutex);

    if (route != MESSAGING_SERVICE_ROUTE_ALL && pid != 0) {
        for (size_t i = 0; i < messaging_service_state.number_of_clients; i++) {
            messaging_service_client_t *client = &messaging_service_state.clients[i];

            if (client->pid == pid) {
                messaging_service_send_to_client(client, data, len);

                delivered = TRUE;
            }
        }
    }

    if (delivered == FALSE && route != MESSAGING_SERVICE_ROUTE_PID_ONLY) {
        for (size_t i = 0; i < messaging_service_state.number_of_clients; i++) {
            messaging_service_send_to_client(&messaging_service_state.clients[i], data, len);
        }
    }

    pthread_mutex_unlock(&messaging_service_state.mutex);
}

static void messaging_service_accept(void) {
    struct ucred credentials = { 0 };
    socklen_t credentials_len = sizeof(credentials);
    int snd_buffer_size = MESSAGING_SERVICE_SND_BUFFER_SIZE;
    gboolean accepted = FALSE;
    int client_fd = accept4(messaging_service_state.server_fd, NULL, NULL, SOCK_CLOEXEC);

    if (client_fd < 0) {
        if (errno != EINTR && errno != EAGAIN) {
            LOG_E(messaging_service_tag, "accept failed: %s", strerror(errno));

            usleep(100000);
        }

        return;
    }

    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &credentials, &credentials_len) < 0) {
        credentials.pid = 0;
    }

    setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &snd_buffer_size, sizeof(snd_buffer_size));

    pthread_mutex_lock(&messaging_service_state.mutex);

    if (messaging_service_state.number_of_clients < MESSAGING_SERVICE_MAX_CLIENTS) {
        messaging_service_client_t *client = &messaging_service_state.clients[messaging_service_state.number_of_clients];

        memset(client, 0, sizeof(*client));

        client->fd = client_fd;
        client->pid = credentials.pid;

        messaging_service_state.number_of_clients += 1;
        accepted = TRUE;

        // A late client still needs to know the head unit resolution

        if (messaging_service_screen_width && messaging_service_screen_height) {
            messaging_service_parcel_screen_info_t message;

            messaging_service_fill_screen_info(&message, messaging_service_screen_width, messaging_service_screen_height);
            messaging_service_send_to_client(client, (uint8_t*)&message, sizeof(message));
        }
    }

    pthread_mutex_unlock(&messaging_service_state.mutex);

    if (accepted == FALSE) {
        LOG_E(messaging_service_tag, "Too many clients, rejecting pid %d", credentials.pid);

        close(client_fd);

        return;
    }

    LOG_I(messaging_service_tag, "Client connected! (fd: %d, pid: %d)", client_fd, credentials.pid);

    // Let a late client know whether it is on screen

    pid_t active_pid = video_receiver_active_pid();

    messaging_service_notify_sources(active_pid, active_pid, video_receiver_sources_count());
}

// Returns FALSE when the client must be removed. Requested source changes are returned
// through out parameters, so they are applied without holding the messaging lock.
static gboolean messaging_service_read_client(messaging_service_client_t *client, int *out_command) {
    ssize_t received = recv(
        client->fd,
        client->rx_buffer + client->rx_filled,
        sizeof(client->rx_buffer) - client->rx_filled,
        MSG_DONTWAIT
    );

    if (received == 0) {
        return FALSE;
    }

    if (received < 0) {
        return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
    }

    client->rx_filled += received;

    while (client->rx_filled >= sizeof(messaging_service_parcel_header_t)) {
        messaging_service_parcel_header_t *header = (messaging_service_parcel_header_t*)client->rx_buffer;
        size_t full_len = bswap_32(header->full_len);

        if (header->mark != bswap_32(MESSAGING_SERVICE_MARK) ||
            full_len < sizeof(messaging_service_parcel_header_t) ||
            full_len > sizeof(client->rx_buffer)) {
            LOG_E(messaging_service_tag, "Bad message from client %d", client->fd);

            return FALSE;
        }

        if (client->rx_filled < full_len) {
            break;
        }

        if (bswap_32(header->event_id) == MESSAGING_SERVICE_SOURCE_CONTROL_EVENT &&
            full_len >= sizeof(messaging_service_parcel_source_control_t)) {
            messaging_service_parcel_source_control_t *control = (messaging_service_parcel_source_control_t*)client->rx_buffer;

            *out_command = control->event.command;
        }

        memmove(client->rx_buffer, client->rx_buffer + full_len, client->rx_filled - full_len);

        client->rx_filled -= full_len;
    }

    return TRUE;
}

static void * messaging_service_handle_clients(void * context) {
    struct pollfd poll_fds[MESSAGING_SERVICE_MAX_CLIENTS + 1];

    (void)context;

    while (1) {
        size_t count;

        poll_fds[0].fd = messaging_service_state.server_fd;
        poll_fds[0].events = POLLIN;

        pthread_mutex_lock(&messaging_service_state.mutex);

        count = messaging_service_state.number_of_clients;

        for (size_t i = 0; i < count; i++) {
            poll_fds[i + 1].fd = messaging_service_state.clients[i].fd;
            poll_fds[i + 1].events = POLLIN;
        }

        pthread_mutex_unlock(&messaging_service_state.mutex);

        if (poll(poll_fds, count + 1, -1) < 0) {
            if (errno != EINTR) {
                usleep(100000);
            }

            continue;
        }

        // Only this thread adds or removes clients, so indexes stay valid

        for (size_t i = count; i > 0; i--) {
            int command = 0;
            pid_t pid = 0;
            gboolean keep = TRUE;

            if (poll_fds[i].revents == 0) {
                continue;
            }

            pthread_mutex_lock(&messaging_service_state.mutex);

            messaging_service_client_t *client = &messaging_service_state.clients[i - 1];

            pid = client->pid;
            keep = messaging_service_read_client(client, &command);

            if (keep == FALSE) {
                LOG_I(messaging_service_tag, "Client disconnected! (fd: %d, pid: %d)", client->fd, client->pid);

                close(client->fd);

                messaging_service_state.clients[i - 1] = messaging_service_state.clients[--messaging_service_state.number_of_clients];
            }

            pthread_mutex_unlock(&messaging_service_state.mutex);

            if (command == MESSAGING_SERVICE_SOURCE_CONTROL_REQUEST_ACTIVE) {
                LOG_I(messaging_service_tag, "pid %d requests the screen", pid);

                video_receiver_request_active(pid);
            } else if (command == MESSAGING_SERVICE_SOURCE_CONTROL_YIELD) {
                LOG_I(messaging_service_tag, "pid %d yields the screen", pid);

                video_receiver_yield(pid);
            }
        }

        if (poll_fds[0].revents & POLLIN) {
            messaging_service_accept();
        }
    }

    return NULL;
}
