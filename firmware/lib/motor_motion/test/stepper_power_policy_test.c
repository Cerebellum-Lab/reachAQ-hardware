#undef NDEBUG /* the checks call the functions under test inside assert() */

#include "stepper_power_policy.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#define T 30000U

#define FAULT STEPPER_POWER_STATUS_DRIVER_FAULT
#define OFF   STEPPER_POWER_STATUS_ENABLE_OFF

static stepper_power_policy_t v13(uint32_t now) {
    stepper_power_policy_t p;
    stepper_power_policy_init(&p, true);
    stepper_power_policy_set_ready(&p, true, now);
    return p;
}

/* Two consecutive polls at `mask` while enabled, at 100 ms and 200 ms. */
static void latch_fault(stepper_power_policy_t *p, uint8_t mask) {
    assert(stepper_power_policy_poll(p, mask, 100, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(p, mask, 200, T) == STEPPER_POWER_ACT_STOP_FAULT);
}

/* Let the host timer run out on a policy built with v13(0). */
static void go_silent(stepper_power_policy_t *p) {
    assert(stepper_power_policy_poll(p, 0, T, T) == STEPPER_POWER_ACT_STOP_SILENT);
}

static void test_v12_board_is_inert(void) {
    stepper_power_policy_t p;
    stepper_power_policy_init(&p, false);
    assert(!stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == 0 && stepper_power_policy_recover(&p) == 0);
    assert(stepper_power_policy_poll(&p, 0x7, 10 * T, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x7, 20 * T, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_status_bits(&p, 0) == 0);
}

static void test_estop_gates_a_board_without_enable_too(void) {
    stepper_power_policy_t p;
    stepper_power_policy_init(&p, false);
    stepper_power_policy_set_estop(&p, true);
    assert(!stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == -EPERM);
    assert(stepper_power_policy_recover(&p) == -EPERM);
    assert(stepper_power_policy_status_bits(&p, 0) == 0);
    stepper_power_policy_set_estop(&p, false);
    assert(stepper_power_policy_motion_error(&p) == 0);
    assert(stepper_power_policy_recover(&p) == 0);
}

static void test_estop_on_a_v13_board_reports_enable_off(void) {
    stepper_power_policy_t p = v13(0);
    stepper_power_policy_set_estop(&p, true);
    assert(!stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_status_bits(&p, 0) == OFF);
    stepper_power_policy_set_estop(&p, false);
    assert(stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_status_bits(&p, 0) == 0);
}

static void test_unproven_drivers_stay_off(void) {
    stepper_power_policy_t p;
    stepper_power_policy_init(&p, true);
    stepper_power_policy_set_ready(&p, false, 0);
    assert(!stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == -EIO);
    assert(stepper_power_policy_recover(&p) == -EIO);
    for (uint8_t id = 0; id < 3; id++) {
        assert(stepper_power_policy_status_bits(&p, id) == OFF);
    }
    assert(stepper_power_policy_poll(&p, 0x7, 100, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x7, 200, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x7, 2 * T, T) == STEPPER_POWER_ACT_NONE);
    assert(!stepper_power_policy_enable_wanted(&p));
}

static void test_ready_enables(void) {
    stepper_power_policy_t p = v13(0);
    assert(stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == 0);
    assert(stepper_power_policy_status_bits(&p, 0) == 0);
}

static void test_one_diag_poll_is_a_glitch(void) {
    stepper_power_policy_t p = v13(0);
    assert(stepper_power_policy_poll(&p, 0x2, 100, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x0, 200, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x2, 300, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_enable_wanted(&p));
}

static void test_two_diag_polls_latch_once(void) {
    stepper_power_policy_t p = v13(0);
    assert(stepper_power_policy_poll(&p, 0x2, 100, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x2, 200, T) == STEPPER_POWER_ACT_STOP_FAULT);
    assert(!stepper_power_policy_enable_wanted(&p) && stepper_power_policy_motion_error(&p) == -EIO);
    assert(stepper_power_policy_status_bits(&p, 1) == (FAULT | OFF));
    assert(stepper_power_policy_status_bits(&p, 0) == OFF);
    assert(stepper_power_policy_poll(&p, 0x2, 300, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x0, 400, T) == STEPPER_POWER_ACT_NONE); /* DIAG falling clears nothing */
    assert(stepper_power_policy_motion_error(&p) == -EIO);
}

static void test_two_drivers_fault_together(void) {
    stepper_power_policy_t p = v13(0);
    latch_fault(&p, 0x5);
    assert(stepper_power_policy_status_bits(&p, 0) == (FAULT | OFF));
    assert(stepper_power_policy_status_bits(&p, 1) == OFF);
    assert(stepper_power_policy_status_bits(&p, 2) == (FAULT | OFF));
    assert(stepper_power_policy_status_bits(&p, 8) == OFF); /* an id past the mask is never faulted */
}

static void test_recover_clears_and_a_persisting_fault_relatches(void) {
    stepper_power_policy_t p = v13(0);
    latch_fault(&p, 0x2);
    assert(stepper_power_policy_recover(&p) == 0);
    assert(stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_status_bits(&p, 1) == 0);
    assert(stepper_power_policy_poll(&p, 0x2, 300, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x2, 400, T) == STEPPER_POWER_ACT_STOP_FAULT);
    assert(!stepper_power_policy_enable_wanted(&p));
}

static void test_recover_clears_fault_but_reports_silence(void) {
    stepper_power_policy_t p = v13(0);
    latch_fault(&p, 0x2);
    go_silent(&p);
    assert(stepper_power_policy_motion_error(&p) == -EIO); /* the fault outranks silence */
    assert(stepper_power_policy_recover(&p) == -ETIMEDOUT);
    assert(stepper_power_policy_status_bits(&p, 1) == OFF);
    assert(stepper_power_policy_motion_error(&p) == -ETIMEDOUT);
    assert(!stepper_power_policy_enable_wanted(&p));
    stepper_power_policy_host_frame(&p, T + 10);
    assert(stepper_power_policy_enable_wanted(&p));
}

static void test_diag_ignored_while_enable_off(void) {
    stepper_power_policy_t p = v13(0);
    go_silent(&p);
    assert(stepper_power_policy_poll(&p, 0x7, T + 100, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0x7, T + 200, T) == STEPPER_POWER_ACT_NONE);
    stepper_power_policy_host_frame(&p, T + 300);
    assert(stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_status_bits(&p, 0) == 0);
    /* what DIAG showed while the enable was off must not count towards the confirm */
    assert(stepper_power_policy_poll(&p, 0x7, T + 400, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_enable_wanted(&p));
}

static void test_estop_outranks_fault_and_release_keeps_it(void) {
    stepper_power_policy_t p = v13(0);
    latch_fault(&p, 0x2);
    stepper_power_policy_set_estop(&p, true);
    assert(stepper_power_policy_motion_error(&p) == -EPERM);
    assert(stepper_power_policy_recover(&p) == -EPERM);
    assert(stepper_power_policy_status_bits(&p, 1) == OFF);
    stepper_power_policy_set_estop(&p, false);
    assert(stepper_power_policy_motion_error(&p) == -EIO); /* the refused recover cleared nothing */
    assert(stepper_power_policy_status_bits(&p, 1) == (FAULT | OFF));
    assert(stepper_power_policy_recover(&p) == 0);
    assert(stepper_power_policy_enable_wanted(&p));
}

static void test_silence_boundary(void) {
    stepper_power_policy_t p = v13(1000);
    assert(stepper_power_policy_poll(&p, 0, 1000 + T - 1, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_poll(&p, 0, 1000 + T, T) == STEPPER_POWER_ACT_STOP_SILENT);
    assert(!stepper_power_policy_enable_wanted(&p) && stepper_power_policy_motion_error(&p) == -ETIMEDOUT);
    assert(stepper_power_policy_status_bits(&p, 0) == OFF);
    assert(stepper_power_policy_poll(&p, 0, 1000 + T + 100, T) == STEPPER_POWER_ACT_NONE);
}

static void test_host_frame_restores_and_restarts_timer(void) {
    stepper_power_policy_t p = v13(0);
    go_silent(&p);
    stepper_power_policy_host_frame(&p, T + 50);
    assert(stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == 0);
    assert(stepper_power_policy_poll(&p, 0, T + 50 + T - 1, T) == STEPPER_POWER_ACT_NONE);
    assert(stepper_power_policy_poll(&p, 0, T + 50 + T, T) == STEPPER_POWER_ACT_STOP_SILENT);
}

static void test_host_frame_does_not_clear_a_fault(void) {
    stepper_power_policy_t p = v13(0);
    latch_fault(&p, 0x2);
    stepper_power_policy_host_frame(&p, 300);
    assert(!stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == -EIO);
}

static void test_silence_survives_wrap(void) {
    stepper_power_policy_t p = v13(UINT32_MAX - 10);
    assert(stepper_power_policy_poll(&p, 0, UINT32_MAX - 5, T) == STEPPER_POWER_ACT_NONE); /* before the wrap */
    assert(stepper_power_policy_poll(&p, 0, 5U, T) == STEPPER_POWER_ACT_NONE);             /* after the wrap */
    assert(stepper_power_policy_poll(&p, 0, UINT32_MAX - 10 + T, T) == STEPPER_POWER_ACT_STOP_SILENT);
}

static void test_recover_while_silent_is_refused(void) {
    stepper_power_policy_t p = v13(0);
    go_silent(&p);
    assert(stepper_power_policy_recover(&p) == -ETIMEDOUT);
    assert(!stepper_power_policy_enable_wanted(&p));
    assert(stepper_power_policy_motion_error(&p) == -ETIMEDOUT);
}

int main(void) {
    test_v12_board_is_inert();
    test_estop_gates_a_board_without_enable_too();
    test_estop_on_a_v13_board_reports_enable_off();
    test_unproven_drivers_stay_off();
    test_ready_enables();
    test_one_diag_poll_is_a_glitch();
    test_two_diag_polls_latch_once();
    test_two_drivers_fault_together();
    test_recover_clears_and_a_persisting_fault_relatches();
    test_recover_clears_fault_but_reports_silence();
    test_diag_ignored_while_enable_off();
    test_estop_outranks_fault_and_release_keeps_it();
    test_silence_boundary();
    test_host_frame_restores_and_restarts_timer();
    test_host_frame_does_not_clear_a_fault();
    test_silence_survives_wrap();
    test_recover_while_silent_is_refused();
    printf("stepper_power_policy_test: 17 passed\n");
    return 0;
}
