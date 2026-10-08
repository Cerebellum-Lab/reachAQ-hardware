/*
    Host-run checks of the timing trailer packer.

        cmake -S firmware/lib/jerrycan/test -B /tmp/t2-host-test -GNinja
        cmake --build /tmp/t2-host-test && /tmp/t2-host-test/timing_trailer_test

    The expected bytes are what reachAQ's decoder unpacks with struct "<BBBBIIQ";
    the first vector is the one its socketcan_backend_test uses.
*/
#include <stdio.h>
#include <string.h>

#include "timing_trailer.h"

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void test_layout_matches_host_struct(void) {
    uint8_t frame[64];
    memset(frame, 0xEE, sizeof(frame));
    const jerrycan_timing_t timing = {
        .kind = JERRYCAN_TIMESTAMP_PHYSICAL_START,
        .flags = JERRYCAN_TIMING_FLAG_COALESCED,
        .boot_id = 0x12345678u,
        .sequence = 99u,
        .board_time_us = 7654321u,
    };
    const uint8_t expected[JERRYCAN_TIMING_TRAILER_SIZE] = {
        0xA7, 0x01, 0x03, 0x01,                         /* tag, version, kind, flags */
        0x78, 0x56, 0x34, 0x12,                         /* boot_id */
        0x63, 0x00, 0x00, 0x00,                         /* sequence */
        0xB1, 0xCB, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, /* board_time_us */
    };

    const size_t used = jerrycan_timing_append_trailer(frame, 6, sizeof(frame), &timing);

    CHECK(used == 26);
    CHECK(memcmp(frame + 6, expected, sizeof(expected)) == 0);
    CHECK(frame[5] == 0xEE);
    CHECK(frame[26] == 0xEE);
}

static void test_largest_payload_that_still_fits(void) {
    uint8_t frame[64] = {0};
    const jerrycan_timing_t timing = {0};

    /* 43-byte payload + uuid = 44, + 20 = 64: fits exactly */
    CHECK(jerrycan_timing_append_trailer(frame, 44, sizeof(frame), &timing) == 64);
    /* 44-byte payload + uuid = 45: no room, length unchanged */
    CHECK(jerrycan_timing_append_trailer(frame, 45, sizeof(frame), &timing) == 45);
}

static void test_full_frame_is_left_alone(void) {
    uint8_t frame[64];
    memset(frame, 0x5A, sizeof(frame));
    const jerrycan_timing_t timing = {.kind = JERRYCAN_TIMESTAMP_OBSERVED};

    CHECK(jerrycan_timing_append_trailer(frame, 64, sizeof(frame), &timing) == 64);
    for (size_t i = 0; i < sizeof(frame); i++) {
        CHECK(frame[i] == 0x5A);
    }
}

static void test_used_beyond_capacity_is_left_alone(void) {
    uint8_t frame[64] = {0};
    const jerrycan_timing_t timing = {0};

    CHECK(jerrycan_timing_append_trailer(frame, 70, sizeof(frame), &timing) == 70);
}

static void test_board_time_uses_all_eight_bytes(void) {
    uint8_t frame[64] = {0};
    const jerrycan_timing_t timing = {.board_time_us = 0x0102030405060708ull};
    const uint8_t expected[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

    jerrycan_timing_append_trailer(frame, 0, sizeof(frame), &timing);

    CHECK(memcmp(frame + 12, expected, sizeof(expected)) == 0);
}

int main(void) {
    test_layout_matches_host_struct();
    test_largest_payload_that_still_fits();
    test_full_frame_is_left_alone();
    test_used_beyond_capacity_is_left_alone();
    test_board_time_uses_all_eight_bytes();

    if (failures != 0) {
        fprintf(stderr, "timing_trailer_test: %d check(s) failed\n", failures);
        return 1;
    }
    puts("timing_trailer_test: all checks passed");
    return 0;
}
