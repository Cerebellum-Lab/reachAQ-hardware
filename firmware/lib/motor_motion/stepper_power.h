#pragma once

/* Drives the shared stepper enable (~EN) and watches DIAG on the pellet v1.3 board. The decisions are in
 * stepper_power_policy.h. A board with no enable net stays inert: only ESTOP stops and refuses motion there.
 *
 * Safe from an ISR: stepper_power_estop_active, stepper_power_motion_error, stepper_power_status_bits and
 * stepper_power_present. stepper_power_init, stepper_power_poll and stepper_power_recover touch the UART, so they
 * run in thread context only.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_DT_HAS_LL_STEPPER_POWER_ENABLED

void stepper_power_init(void);       /* detect, prove drivers, first EN write */
void stepper_power_poll(void);       /* motor work queue, every STEPPER_DRIVER_CHECK_PERIOD */
void stepper_power_host_frame(void); /* jerrycan hook and shell motion commands */
void stepper_power_set_estop(bool asserted);
bool stepper_power_estop_active(void);
int stepper_power_motion_error(void);
int stepper_power_recover(void); /* called by a re-home; re-proves the drivers if a fault is latched */
uint8_t stepper_power_status_bits(uint8_t motor_id);
bool stepper_power_present(void);             /* has_enable */
void stepper_power_force_off_for_fault(void); /* interrupts locked: takes no spinlock */

/* What `motor_math stepper_power_status` prints. */
typedef struct {
    bool detected;
    bool ready;
    bool silent;
    bool estop;
    bool enable_on;
    uint8_t fault_mask;
    uint8_t diag_mask; /* live pins; 0 if no enable net, where they are never configured */
} stepper_power_state_t;

void stepper_power_read_state(stepper_power_state_t *state);

#else

static inline void stepper_power_init(void) {}
static inline void stepper_power_poll(void) {}
static inline void stepper_power_host_frame(void) {}
static inline void stepper_power_set_estop(bool asserted) { (void)asserted; }
static inline bool stepper_power_estop_active(void) { return false; }
static inline int stepper_power_motion_error(void) { return 0; }
static inline int stepper_power_recover(void) { return 0; }
static inline uint8_t stepper_power_status_bits(uint8_t motor_id) {
    (void)motor_id;
    return 0U;
}
static inline bool stepper_power_present(void) { return false; }
static inline void stepper_power_force_off_for_fault(void) {}

#endif
