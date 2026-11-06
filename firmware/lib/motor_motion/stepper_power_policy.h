#pragma once

/* Policy for the shared stepper enable (~EN) on the pellet v1.3 board.
 *
 * Pure logic: no Zephyr, no GPIO, no clock. The caller feeds in what it sampled and acts on what comes back.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#define STEPPER_POWER_STATUS_DRIVER_FAULT 0x01U /* this driver raised DIAG; a re-home clears it */
#define STEPPER_POWER_STATUS_ENABLE_OFF   0x02U /* the shared enable is off, for any reason */

typedef enum {
    STEPPER_POWER_ACT_NONE = 0,
    STEPPER_POWER_ACT_STOP_FAULT,  /* stop steppers, capture DRV_STATUS, mark all axes unhomed */
    STEPPER_POWER_ACT_STOP_SILENT, /* stop steppers, mark the ones that were moving unhomed */
} stepper_power_action_t;

typedef struct {
    bool has_enable;    /* PD2 read high against its pull-down at boot */
    bool drivers_ready; /* every driver proved configured */
    bool estop;         /* asserted until an ESTOP with payload 0 */
    bool host_silent;
    uint8_t fault_mask; /* latched; bit n = driver n */
    uint8_t diag_prev;  /* last poll's reading while enabled, for the two-poll confirm */
    uint32_t last_host_ms;
} stepper_power_policy_t;

void stepper_power_policy_init(stepper_power_policy_t *p, bool has_enable);
void stepper_power_policy_set_ready(stepper_power_policy_t *p, bool ready, uint32_t now_ms);
stepper_power_action_t stepper_power_policy_poll(stepper_power_policy_t *p, uint8_t diag_mask, uint32_t now_ms,
                                                 uint32_t silence_timeout_ms);
void stepper_power_policy_host_frame(stepper_power_policy_t *p, uint32_t now_ms);
void stepper_power_policy_set_estop(stepper_power_policy_t *p, bool asserted);
bool stepper_power_policy_enable_wanted(const stepper_power_policy_t *p);
int stepper_power_policy_motion_error(const stepper_power_policy_t *p); /* also the final ACK of an interrupted motion */
int stepper_power_policy_recover(stepper_power_policy_t *p);            /* called by a re-home */
uint8_t stepper_power_policy_status_bits(const stepper_power_policy_t *p, uint8_t motor_id);
