// Accessory session tests: the real usb_active_accessory.c talks to a pseudo terminal
// instead of /dev/usb_accessory, the test plays the head unit on the master side.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <termios.h>
#include <byteswap.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "messages/v2/message_common_header_v2.h"
#include "services/video_receiver/video_receiver.h"
#include "services/messaging_service/messaging_service.h"
#include "usb_accessory/usb_accessory.h"
#include "usb_accessory/usb_active_accessory.h"

static int failures = 0;
static int gadget_disable_calls = 0;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
        failures++;                                             \
        printf("  FAIL %s:%d: ", __FILE__, __LINE__);           \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
    }                                                           \
} while (0)

#pragma mark - Fake gadget (configfs is not available in tests)

int usb_accessory_init(void) { return 0; }
int usb_accessory_set_mode(usb_accessory_mode_t mode) { (void)mode; return 0; }
int usb_accessory_disable(void) { __atomic_add_fetch(&gadget_disable_calls, 1, __ATOMIC_SEQ_CST); return 0; }

#pragma mark - Helpers

static int master_fd = -1;

static void open_pty(void) {
    struct termios tio;

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    grantpt(master_fd);
    unlockpt(master_fd);

    // Raw mode on the slave side: bytes must pass unchanged
    int slave = open(ptsname(master_fd), O_RDWR | O_NOCTTY);
    tcgetattr(slave, &tio);
    cfmakeraw(&tio);
    tcsetattr(slave, TCSANOW, &tio);
    close(slave);

    setenv("QDPLAY_ACCESSORY_DEVICE", ptsname(master_fd), 1);
}

static int connect_video_source(void) {
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    strncpy(addr.sun_path, "/tmp/qdplay.video.socket", sizeof(addr.sun_path) - 1);

    for (int i = 0; i < 100; i++) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        usleep(10000);
    }

    return -1;
}

static size_t build_v2_json(uint8_t *out, const char *json) {
    message_common_header_v2_t *header = (message_common_header_v2_t*)out;
    size_t full = sizeof(*header) + strlen(json);
    size_t padded = (full + 511) & ~(size_t)511;

    memset(out, 0, padded);
    memcpy(header->mark, "5A5A", 4);
    header->full_message_len = bswap_32((uint32_t)full);
    header->source = MESSAGE_COMMON_HEADER_V2_SOURCE_CAR;
    header->payload_format = MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON;
    memcpy(out + sizeof(*header), json, strlen(json));

    return padded;
}

/* Reads from the "head unit" side for `ms`, returns the number of times `needle` was seen */
static int hu_read_count(const char *needle, int ms) {
    static char acc[1 << 16];
    size_t acc_len = 0;
    int count = 0;
    gint64 end = g_get_monotonic_time() + ms * 1000;

    while (g_get_monotonic_time() < end) {
        struct pollfd p = { .fd = master_fd, .events = POLLIN };

        if (poll(&p, 1, 50) > 0) {
            ssize_t n = read(master_fd, acc + acc_len, sizeof(acc) - acc_len);

            if (n > 0) acc_len += (size_t)n;
            if (acc_len > sizeof(acc) - 1024) acc_len = 0;
        }
    }

    for (size_t i = 0; needle && i + strlen(needle) <= acc_len; i++) {
        if (memcmp(acc + i, needle, strlen(needle)) == 0) count++;
    }

    return count;
}

typedef struct {
    int result;
    gint64 duration_ms;
    volatile int done;
} session_run_t;

static void *session_thread(void *arg) {
    session_run_t *run = arg;
    gint64 start = g_get_monotonic_time();

    run->result = usb_active_accessory_run_session();
    run->duration_ms = (g_get_monotonic_time() - start) / 1000;
    run->done = 1;

    return NULL;
}

static void wait_done(session_run_t *run, int max_ms) {
    for (int i = 0; i < max_ms / 10 && !run->done; i++) usleep(10000);
}

#pragma mark - Tests

static void test_head_unit_never_connects(void) {
    session_run_t run = { 0 };
    pthread_t thread;

    printf("head unit never answers: blocked read() must be interrupted by the watchdog\n");

    open_pty();
    pthread_create(&thread, NULL, session_thread, &run);

    hu_read_count(NULL, 500);   /* swallow app_status, never answer */
    wait_done(&run, 15000);

    CHECK(run.done, "session thread still blocked after 15 s");
    if (run.done) {
        CHECK(run.duration_ms >= 9000 && run.duration_ms <= 12000, "connect timeout ~10 s, got %lld ms", (long long)run.duration_ms);
        pthread_join(thread, NULL);
    }

    close(master_fd);
}

static void test_v2_session_then_silence(void) {
    session_run_t run = { 0 };
    pthread_t thread;
    uint8_t message[1024];

    printf("V2 session: handshake, heartbeats, then the head unit goes silent\n");

    open_pty();
    pthread_create(&thread, NULL, session_thread, &run);

    CHECK(hu_read_count("!BIN", 300) >= 1, "app_status sent first");

    size_t len = build_v2_json(message, "{\"CMD\":\"CAR_INFO\",\"PARA\":{\"CarWidth\":1280,\"CarHeight\":720}}");
    write(master_fd, message, len);

    CHECK(hu_read_count("PHONE_INFO", 500) == 1, "PHONE_INFO answered");

    // Keep talking for 4 s: our heartbeats must arrive every ~3 s, the session must stay up
    len = build_v2_json(message, "{\"CMD\":\"HEARTBEAT\"}");
    int heartbeats = 0;
    gint64 silent_since = 0;

    for (int i = 0; i < 4; i++) {
        write(master_fd, message, len);
        silent_since = g_get_monotonic_time();
        heartbeats += hu_read_count("HEARTBEAT", 1000);
    }

    CHECK(heartbeats >= 1, "QDPlay sends heartbeats (got %d)", heartbeats);
    CHECK(!run.done, "session alive while the head unit talks");

    // Silence since the last message: V2 timeout is 5 s
    wait_done(&run, 9000);

    CHECK(run.done, "session must end after 5 s of silence");
    if (run.done) {
        gint64 silence_ms = (g_get_monotonic_time() - silent_since) / 1000;
        CHECK(silence_ms >= 4500 && silence_ms <= 7000, "ended after %lld ms of silence", (long long)silence_ms);
        pthread_join(thread, NULL);
    }

    close(master_fd);
}

static void test_link_drop(void) {
    session_run_t run = { 0 };
    pthread_t thread;
    uint8_t message[1024];

    printf("USB link drops mid-session: the session ends at once, not after the timeout\n");

    open_pty();
    pthread_create(&thread, NULL, session_thread, &run);

    hu_read_count(NULL, 300);

    size_t len = build_v2_json(message, "{\"CMD\":\"CAR_INFO\",\"PARA\":{\"CarWidth\":800,\"CarHeight\":480}}");
    write(master_fd, message, len);
    hu_read_count(NULL, 500);

    gint64 dropped = g_get_monotonic_time();
    close(master_fd);   /* the slave side now reads EIO, like f_accessory after a disconnect */

    wait_done(&run, 6000);

    CHECK(run.done, "session must end");
    if (run.done) {
        gint64 ms = (g_get_monotonic_time() - dropped) / 1000;
        CHECK(ms < 1500, "ended %lld ms after the drop", (long long)ms);
        pthread_join(thread, NULL);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    usb_active_accessory_install_signal_handler();

    if (video_receiver_start() != VIDEO_RECEIVER_OK || messaging_service_init() != TRUE) {
        printf("can`t start services\n");
        return 1;
    }

    // The session only runs while a video source is connected
    int video = connect_video_source();
    usleep(200000);
    CHECK(video >= 0 && video_receiver_is_connected(), "video source connected");

    test_head_unit_never_connects();
    test_v2_session_then_silence();
    test_link_drop();

    CHECK(gadget_disable_calls > 0, "gadget is detached on teardown");

    printf(failures ? "FAILED: %d check(s)\n" : "ALL SESSION TESTS PASSED\n", failures);

    return failures ? 1 : 0;
}
