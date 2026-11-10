/*
    Host-run checks of the board clock's extension step: a 32-bit CYCCNT reading extended to 64 bits.

        cmake -S firmware/lib/jerrycan/test -B /tmp/h1-host-test -GNinja
        cmake --build /tmp/h1-host-test && ctest --test-dir /tmp/h1-host-test --output-on-failure
*/
#include <stdint.h>
#include <stdio.h>

#include "board_clock.h"

static int failures;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define WRAP (UINT64_C(1) << 32)

static void test_the_first_reading_is_the_counter_itself(void) {
    board_clock_ext_t ext = {0};
    CHECK(board_clock_extend(&ext, 0x12345678U) == 0x12345678U);
    CHECK(ext.last == 0x12345678U);
    CHECK(ext.wraps == 0);

    /* CYCCNT is not reset at boot, so the first reading can be anywhere below a wrap */
    board_clock_ext_t late = {0};
    CHECK(board_clock_extend(&late, 0xFFFFFF00U) == 0xFFFFFF00U);
    CHECK(late.wraps == 0);
}

static void test_rising_readings_count_no_wrap(void) {
    board_clock_ext_t ext = {0};
    CHECK(board_clock_extend(&ext, 100) == 100);
    CHECK(board_clock_extend(&ext, 170000000U) == 170000000U);
    CHECK(board_clock_extend(&ext, 0xF0000000U) == 0xF0000000U);
    CHECK(ext.wraps == 0);
}

static void test_a_lower_reading_counts_exactly_one_wrap(void) {
    board_clock_ext_t ext = {0};
    CHECK(board_clock_extend(&ext, 0xFFFFFFF0U) == 0xFFFFFFF0U);
    CHECK(board_clock_extend(&ext, 0x10U) == WRAP + 0x10U);
    CHECK(ext.wraps == 1);
    CHECK(board_clock_extend(&ext, 0x20U) == WRAP + 0x20U);
    CHECK(ext.wraps == 1);
}

static void test_an_equal_reading_is_not_a_wrap(void) {
    board_clock_ext_t ext = {0};
    CHECK(board_clock_extend(&ext, 5000) == 5000);
    CHECK(board_clock_extend(&ext, 5000) == 5000);
    CHECK(ext.wraps == 0);

    /* Equal to 0 on a zeroed state, too */
    board_clock_ext_t zero = {0};
    CHECK(board_clock_extend(&zero, 0) == 0);
    CHECK(zero.wraps == 0);
}

static void test_readings_either_side_of_the_wrap_are_one_cycle_apart(void) {
    board_clock_ext_t ext = {0};
    const uint64_t before = board_clock_extend(&ext, 0xFFFFFFFFU);
    const uint64_t after = board_clock_extend(&ext, 0x00000000U);
    CHECK(before == WRAP - 1);
    CHECK(after == WRAP);
    CHECK(after - before == 1);
}

/* A true 64-bit count read through its low 32 bits, 2.7e9 cycles (15.8 s at 170 MHz) apart: less than a wrap */
static void test_many_wraps_track_the_true_count(void) {
    board_clock_ext_t ext = {0};
    const uint64_t step = 0xA0000000U;
    uint64_t truth = 0x40000000U;
    uint64_t previous = 0;
    int mismatches = 0;

    for (int i = 0; i < 200000; i++) {
        const uint64_t extended = board_clock_extend(&ext, (uint32_t)truth);
        if (extended != truth || (i > 0 && extended <= previous)) {
            mismatches++;
        }
        previous = extended;
        truth += step;
    }

    CHECK(mismatches == 0);
    /* 200000 readings of 0.625 wrap each: the high word is well past 1 */
    CHECK(ext.wraps == (uint32_t)((truth - step) >> 32));
    CHECK(ext.wraps > 100000U);
}

/* The high word is shifted as 64 bits: wraps past 2^31 still land in the top word */
static void test_a_large_wrap_count_lands_in_the_high_word(void) {
    board_clock_ext_t ext = {.last = 0xFFFFFFF0U, .wraps = 0x7FFFFFFFU};
    CHECK(board_clock_extend(&ext, 5) == ((UINT64_C(0x80000000) << 32) | 5U));
    CHECK(ext.wraps == 0x80000000U);
}

int main(void) {
    test_the_first_reading_is_the_counter_itself();
    test_rising_readings_count_no_wrap();
    test_a_lower_reading_counts_exactly_one_wrap();
    test_an_equal_reading_is_not_a_wrap();
    test_readings_either_side_of_the_wrap_are_one_cycle_apart();
    test_many_wraps_track_the_true_count();
    test_a_large_wrap_count_lands_in_the_high_word();

    if (failures != 0) {
        fprintf(stderr, "board_clock_test: %d check(s) failed\n", failures);
        return 1;
    }
    puts("board_clock_test: all checks passed");
    return 0;
}
