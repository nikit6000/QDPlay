// QDPlay tests: protocol parsing robustness and video source switching.
// Built against the real sources with a fake USB layer (see fake_usb.c).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <byteswap.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "fake_usb.h"
#include "hu_message.h"
#include "messages/v2/message_common_header_v2.h"
#include "messages/v2/message_touch_event_v2.h"
#include "services/video_receiver/video_receiver.h"
#include "services/messaging_service/messaging_service.h"
#include "usb_accessory/message_processor/v2/message_processor_v2.h"

static int failures = 0;

#define CHECK(cond, ...) do {                                   \
    if (!(cond)) {                                              \
        failures++;                                             \
        printf("  FAIL %s:%d: ", __FILE__, __LINE__);           \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
    }                                                           \
} while (0)

#pragma mark - Helpers

static size_t build_v2_message(uint8_t *out, uint8_t msg_type, uint8_t payload_format,
                               const void *ext, uint16_t ext_len, const void *payload, size_t payload_len) {
    message_common_header_v2_t *header = (message_common_header_v2_t*)out;
    size_t full = sizeof(*header) + ext_len + payload_len;
    size_t padded = (full + 511) & ~(size_t)511;

    memset(out, 0, padded);
    memcpy(header->mark, "5A5A", 4);
    header->full_message_len = bswap_32((uint32_t)full);
    header->extended_header_size = bswap_16(ext_len);
    header->msg_type = msg_type;
    header->source = MESSAGE_COMMON_HEADER_V2_SOURCE_CAR;
    header->payload_format = payload_format;

    if (ext_len) memcpy(out + sizeof(*header), ext, ext_len);
    memcpy(out + sizeof(*header) + ext_len, payload, payload_len);

    return padded;
}

static size_t build_touch_payload(uint8_t *out, uint8_t fingers, uint8_t action, float x, float y) {
    uint32_t action_id = bswap_32(7);
    size_t len = 0;

    memcpy(out, &action_id, 4); len += 4;
    out[len++] = fingers;

    for (uint8_t i = 0; i < fingers; i++) {
        uint32_t fx, fy;
        memcpy(&fx, &x, 4); memcpy(&fy, &y, 4);
        fx = bswap_32(fx); fy = bswap_32(fy);
        out[len++] = i;
        out[len++] = action;
        memcpy(out + len, &fx, 4); len += 4;
        memcpy(out + len, &fy, 4); len += 4;
    }

    return len;
}

static int connect_unix(const char *path) {
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    for (int i = 0; i < 100; i++) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) return fd;
        usleep(10000);
    }

    close(fd);
    return -1;
}

// Reads one messaging event with a timeout. Returns event id or -1.
static int read_event(int fd, int timeout_ms, int *value) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    messaging_service_parcel_header_t header;
    uint8_t payload[64];

    if (poll(&p, 1, timeout_ms) <= 0) return -1;
    if (recv(fd, &header, sizeof(header), MSG_WAITALL) != sizeof(header)) return -1;

    uint32_t payload_len = bswap_32(header.payload_len);

    if (payload_len > sizeof(payload)) return -1;
    if (payload_len && recv(fd, payload, payload_len, MSG_WAITALL) != (ssize_t)payload_len) return -1;
    if (value) *value = payload_len ? payload[0] : 0;

    return (int)bswap_32(header.event_id);
}

// Drains events until `event_id` arrives (with `expected` value, if >= 0). Returns its value or -1.
static int wait_event_value(int fd, int event_id, int expected, int timeout_ms) {
    int value = 0;

    for (int waited = 0; waited < timeout_ms; waited += 50) {
        int ev = read_event(fd, 50, &value);
        if (ev == event_id && (expected < 0 || value == expected)) return value;
    }

    return -1;
}

static int wait_event(int fd, int event_id, int timeout_ms) {
    return wait_event_value(fd, event_id, -1, timeout_ms);
}

static void send_frame_ex(int fd, uint32_t frame_size, int nal_type) {
    uint8_t buffer[sizeof(messaging_service_video_frame_t) + 4096];
    messaging_service_video_frame_t *frame = (messaging_service_video_frame_t*)buffer;
    uint8_t *payload = buffer + sizeof(*frame);

    memset(buffer, 0xAB, sizeof(buffer));
    payload[0] = 0; payload[1] = 0; payload[2] = 0; payload[3] = 1;
    payload[4] = (uint8_t)(0x60 | nal_type);
    frame->header.mark = bswap_32(MESSAGING_SERVICE_MARK);
    frame->header.event_id = bswap_32(MESSAGING_SERVICE_H264_FRAME_EVENT);
    frame->header.full_len = bswap_32(sizeof(*frame) + frame_size);
    frame->header.payload_len = bswap_32(sizeof(frame->event) + frame_size);
    frame->event.width = bswap_32(800);
    frame->event.height = bswap_32(480);
    frame->event.frame_rate = bswap_32(30);
    frame->event.frame_size = bswap_32(frame_size);

    send(fd, buffer, sizeof(*frame) + frame_size, MSG_NOSIGNAL);
}

// Every 10th frame is an IDR, like a real encoder with a short GOP
static void send_frame(int fd, uint32_t frame_size) {
    static int counter = 0;

    send_frame_ex(fd, frame_size, (counter++ % 10) == 0 ? 5 : 1);
}

static void send_source_control(int fd, uint8_t command) {
    messaging_service_parcel_source_control_t message = {
        .header = {
            .mark = bswap_32(MESSAGING_SERVICE_MARK),
            .event_id = bswap_32(MESSAGING_SERVICE_SOURCE_CONTROL_EVENT),
            .full_len = bswap_32(sizeof(message)),
            .payload_len = bswap_32(1)
        },
        .event = { .command = command }
    };

    send(fd, &message, sizeof(message), MSG_NOSIGNAL);
}

#pragma mark - Tests

static void test_hu_message_decode_fuzz(void) {
    printf("hu_message_decode: malformed input\n");

    hu_message_t *message = hu_message_init("QDRIVE_ASSISTANT", "HUINFO", 2, "{\"a\":1}", "xyz");
    uint32_t len = 0;
    uint8_t *encoded = hu_message_encode(message, 0, &len);

    hu_message_t *decoded = hu_message_decode(encoded, len);
    CHECK(decoded && strcmp(decoded->logic_id, "HUINFO") == 0 && decoded->data_count == 2, "round trip");
    hu_message_free(decoded);

    // Every truncation must be rejected or parsed without reading past the buffer (ASan)
    for (uint32_t cut = 0; cut < len; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, encoded, cut);
        hu_message_free(hu_message_decode(copy, cut));
        free(copy);
    }

    // Random garbage after a valid mark
    srand(1);
    for (int i = 0; i < 20000; i++) {
        uint8_t buf[64];
        for (size_t j = 0; j < sizeof(buf); j++) buf[j] = "0123456789abcdefA6"[rand() % 18];
        memcpy(buf, "A6A6", 4);
        uint32_t l = 4 + rand() % 60;
        uint8_t *copy = malloc(l);
        memcpy(copy, buf, l);
        hu_message_free(hu_message_decode(copy, l));
        free(copy);
    }

    free(encoded);
    hu_message_free(message);
}

static void test_touch_parse(void) {
    uint8_t payload[256];
    message_touches_v2_t touches;

    printf("touch parser\n");

    size_t len = build_touch_payload(payload, 0, 1, 10, 10);
    CHECK(message_touch_v2_parse(payload, len, &touches) < 0, "zero fingers must be rejected (was a division by zero)");

    len = build_touch_payload(payload, 11, 1, 10, 10);
    CHECK(message_touch_v2_parse(payload, len, &touches) < 0, "too many fingers");

    len = build_touch_payload(payload, 2, 1, 100.5f, 200.25f);
    CHECK(message_touch_v2_parse(payload, len - 1, &touches) < 0, "truncated payload");
    CHECK(message_touch_v2_parse(payload, len, &touches) == 0, "valid payload");
    CHECK(touches.fingers_count == 2 && touches.fingers[1].x == 100.5f && touches.fingers[1].y == 200.25f, "coordinates");

    // Padding after the fingers is allowed
    CHECK(message_touch_v2_parse(payload, 496, &touches) == 0, "padded payload");
}

static int count_occurrences(const uint8_t *data, size_t len, const char *needle) {
    int count = 0;
    size_t nlen = strlen(needle);

    for (size_t i = 0; i + nlen <= len; i++) {
        if (memcmp(data + i, needle, nlen) == 0) count++;
    }

    return count;
}

static void test_v2_probe_long_message(void) {
    uint8_t message[4096];
    char json[2048];
    uint8_t ext[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint8_t *tx;

    printf("V2 probe: CAR_INFO longer than 1024 bytes, extended header, split transfers\n");

    // Long CAR_INFO: the old code failed for any message longer than 1024 bytes
    int n = snprintf(json, sizeof(json), "{\"CMD\":\"CAR_INFO\",\"PARA\":{\"CarWidth\":1760,\"CarHeight\":720,\"Pad\":\"");
    while (n < 1300) json[n++] = 'x';
    n += snprintf(json + n, sizeof(json) - n, "\"}}");

    size_t padded = build_v2_message(message, 0, MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON, ext, sizeof(ext), json, n);

    fake_usb_reset();

    // The probe receives the first 512 bytes, the rest arrives as two more transfers
    fake_usb_queue_transfer(message + 512, 512);
    fake_usb_queue_transfer(message + 1024, padded - 1024);

    CHECK(message_processor_v2_get()->probe(message, 512) == TRUE, "probe must succeed");

    size_t tx_len = fake_usb_tx(&tx);
    CHECK(count_occurrences(tx, tx_len, "\"PHONE_INFO\"") == 1, "PHONE_INFO sent");
    CHECK(count_occurrences(tx, tx_len, "\"UPDATE_NOTIFY\"") == 1, "UPDATE_NOTIFY sent");
    CHECK(count_occurrences(tx, tx_len, "\"MirrorWidth\":1760") == 1, "resolution taken from CAR_INFO");
    CHECK(tx_len % 512 == 0, "writes padded to 512");

    // Garbage length must not crash or allocate gigabytes
    message_common_header_v2_t *header = (message_common_header_v2_t*)message;
    header->full_message_len = bswap_32(0x7FFFFFFF);
    fake_usb_reset();
    CHECK(message_processor_v2_get()->probe(message, 512) == FALSE, "huge length rejected");

    header->full_message_len = bswap_32(4);
    fake_usb_reset();
    CHECK(message_processor_v2_get()->probe(message, 512) == FALSE, "length smaller than header rejected");
}

static void test_v2_run_and_sources(void) {
    uint8_t message[2048];
    uint8_t payload[256];
    int status_pipe[2];

    printf("V2 run + video sources switching (multi-process)\n");

    CHECK(video_receiver_start() == VIDEO_RECEIVER_OK, "video receiver start");
    CHECK(messaging_service_init() == TRUE, "messaging start");

    video_receiver_activate();

    // Two "sources" in separate processes: A (launcher) and B (CarPlay)
    pipe(status_pipe);

    pid_t pid_a = fork();
    if (pid_a == 0) {
        int msg = connect_unix("/tmp/qdplay.messaging.socket");
        usleep(50000);
        int video = connect_unix("/tmp/qdplay.video.socket");
        int active = wait_event_value(msg, MESSAGING_SERVICE_ACTIVE_STATE_EVENT, 1, 2000);
        for (int i = 0; i < 700; i++) { send_frame(video, 1000); usleep(10000); }
        _exit(active == 1 ? 0 : 1);
    }

    usleep(300000);
    CHECK(video_receiver_active_pid() == pid_a, "first source is active");

    int frames_a = fake_usb_h264_frames();
    usleep(200000);
    CHECK(fake_usb_h264_frames() > frames_a, "frames of the active source are forwarded");

    pid_t pid_b = fork();
    if (pid_b == 0) {
        int msg = connect_unix("/tmp/qdplay.messaging.socket");
        usleep(50000);
        int video = connect_unix("/tmp/qdplay.video.socket");
        int active = wait_event_value(msg, MESSAGING_SERVICE_ACTIVE_STATE_EVENT, 1, 2000);
        int keyframe = wait_event(msg, MESSAGING_SERVICE_KEY_FRAME_REQ_EVENT, 2000);
        // P-frames before the first IDR must be held back, then the IDR opens the stream
        for (int i = 0; i < 5; i++) { send_frame_ex(video, 500, 1); usleep(20000); }
        usleep(600000);
        send_frame_ex(video, 500, 5);
        usleep(400000);
        send_source_control(msg, MESSAGING_SERVICE_SOURCE_CONTROL_YIELD);
        usleep(300000);
        close(video);
        close(msg);
        _exit(active == 1 && keyframe == 0 ? 0 : 1);
    }

    usleep(250000);
    CHECK(video_receiver_active_pid() == pid_b, "newest source becomes active");

    int frames_before = fake_usb_h264_frames();
    usleep(250000);
    CHECK(fake_usb_h264_frames() == frames_before, "frames of the old source and P-frames before IDR are dropped");
    usleep(450000);
    CHECK(fake_usb_h264_frames() == frames_before + 1, "IDR of the new source is forwarded (got %d)", fake_usb_h264_frames() - frames_before);

    // Touch while B is active goes only to B (B does not read touches, A must not get them)

    // B yields -> A active again
    usleep(1000000);
    CHECK(video_receiver_active_pid() == pid_a, "yield returns the screen to the previous source");

    // V2 messages from the head unit: VIDEO_CTRL, broken touch, gesture
    const char *video_ctrl = "{\"CMD\":\"VIDEO_CTRL\",\"PARA\":{\"PlayStatus\":1}}";
    size_t len = build_v2_message(message, 0, MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_JSON, NULL, 0, video_ctrl, strlen(video_ctrl));
    fake_usb_reset();
    fake_usb_queue_transfer(message, len);
    CHECK(message_processor_v2_get()->run() == TRUE, "VIDEO_CTRL handled");

    size_t touch_len = build_touch_payload(payload, 0, 1, 1, 1);
    len = build_v2_message(message, MESSAGE_COMMON_HEADER_V2_MSG_TYPE_TOUCH, MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_BIN, NULL, 0, payload, touch_len);
    fake_usb_reset();
    fake_usb_queue_transfer(message, len);
    CHECK(message_processor_v2_get()->run() == TRUE, "zero-finger touch does not crash");

    int status = 0;
    waitpid(pid_b, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "B got ACTIVE_STATE=1 and KEY_FRAME_REQ");

    usleep(200000);
    CHECK(video_receiver_active_pid() == pid_a, "A stays active after B disconnects");

    // Three-finger gesture with only A connected keeps A (nothing to switch to)
    touch_len = build_touch_payload(payload, 3, 1, 50, 50);
    len = build_v2_message(message, MESSAGE_COMMON_HEADER_V2_MSG_TYPE_TOUCH, MESSAGE_COMMON_HEADER_V2_PAYLOAD_FMT_BIN, NULL, 0, payload, touch_len);
    fake_usb_reset();
    fake_usb_queue_transfer(message, len);
    CHECK(message_processor_v2_get()->run() == TRUE, "gesture handled");
    CHECK(video_receiver_active_pid() == pid_a, "single source keeps the screen");

    // Gesture to the launcher, then the launcher yields (tap on "CarPlay"): the screen
    // must go back to C. Both sources used to end up with priority 0 and A kept the screen.
    pid_t pid_c = fork();
    if (pid_c == 0) {
        int video = connect_unix("/tmp/qdplay.video.socket");
        usleep(1500000);
        close(video);
        _exit(0);
    }

    usleep(250000);
    CHECK(video_receiver_active_pid() == pid_c, "C becomes active");
    video_receiver_switch_to_previous();
    CHECK(video_receiver_active_pid() == pid_a, "gesture switches to A");
    video_receiver_yield(pid_a);
    CHECK(video_receiver_active_pid() == pid_c, "A yields back to C");
    video_receiver_switch_to_previous();
    CHECK(video_receiver_active_pid() == pid_a, "second gesture switches to A again");
    waitpid(pid_c, &status, 0);

    waitpid(pid_a, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "A got ACTIVE_STATE=1");

    usleep(200000);
    CHECK(video_receiver_is_connected() == FALSE, "no sources left");
    CHECK(video_receiver_active_pid() == 0, "no active source");
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    test_hu_message_decode_fuzz();
    test_touch_parse();
    test_v2_probe_long_message();
    test_v2_run_and_sources();

    printf(failures ? "FAILED: %d check(s)\n" : "ALL TESTS PASSED\n", failures);

    return failures ? 1 : 0;
}
