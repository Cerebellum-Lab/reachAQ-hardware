#include "stepper_power_policy.h"

/* Precedence of the queries below, first match wins:
 * estop, no enable net, drivers not ready, latched fault, host silent.
 */

void stepper_power_policy_init(stepper_power_policy_t *p, bool has_enable) {
    *p = (stepper_power_policy_t){.has_enable = has_enable};
}

void stepper_power_policy_set_ready(stepper_power_policy_t *p, bool ready, uint32_t now_ms) {
    p->drivers_ready = ready;
    if (ready) {
        p->last_host_ms = now_ms;
    }
}

void stepper_power_policy_host_frame(stepper_power_policy_t *p, uint32_t now_ms) {
    p->host_silent = false;
    p->last_host_ms = now_ms;
}

void stepper_power_policy_set_estop(stepper_power_policy_t *p, bool asserted) {
    p->estop = asserted;
}

bool stepper_power_policy_enable_wanted(const stepper_power_policy_t *p) {
    return p->has_enable && p->drivers_ready && !p->estop && p->fault_mask == 0 && !p->host_silent;
}

stepper_power_action_t stepper_power_policy_poll(stepper_power_policy_t *p, uint8_t diag_mask, uint32_t now_ms,
                                                 uint32_t silence_timeout_ms) {
    if (!p->has_enable || !p->drivers_ready) {
        return STEPPER_POWER_ACT_NONE;
    }

    /* EN off clears DIAG, so a reading only counts while the enable is on. */
    const bool enabled = stepper_power_policy_enable_wanted(p);
    const uint8_t confirmed = enabled ? (uint8_t)(p->diag_prev & diag_mask) : 0U;
    p->diag_prev = enabled ? diag_mask : 0U;

    if (confirmed != 0U) {
        p->fault_mask |= confirmed;
        p->diag_prev = 0U;
        return STEPPER_POWER_ACT_STOP_FAULT;
    }

    if (!p->host_silent && (uint32_t)(now_ms - p->last_host_ms) >= silence_timeout_ms) {
        p->host_silent = true;
        p->diag_prev = 0U;
        return STEPPER_POWER_ACT_STOP_SILENT;
    }

    return STEPPER_POWER_ACT_NONE;
}

int stepper_power_policy_motion_error(const stepper_power_policy_t *p) {
    if (p->estop) {
        return -EPERM;
    }
    if (!p->has_enable) {
        return 0;
    }
    if (!p->drivers_ready || p->fault_mask != 0U) {
        return -EIO;
    }
    if (p->host_silent) {
        return -ETIMEDOUT;
    }
    return 0;
}

int stepper_power_policy_recover(stepper_power_policy_t *p) {
    if (p->estop) {
        return -EPERM;
    }
    if (!p->has_enable) {
        return 0;
    }
    if (!p->drivers_ready) {
        return -EIO;
    }
    if (p->fault_mask != 0U) {
        p->fault_mask = 0U;
        p->diag_prev = 0U;
    }
    return p->host_silent ? -ETIMEDOUT : 0;
}

uint8_t stepper_power_policy_status_bits(const stepper_power_policy_t *p, uint8_t motor_id) {
    if (p->estop) {
        return p->has_enable ? STEPPER_POWER_STATUS_ENABLE_OFF : 0U;
    }
    if (!p->has_enable) {
        return 0U;
    }
    if (!p->drivers_ready) {
        return STEPPER_POWER_STATUS_ENABLE_OFF;
    }
    if (p->fault_mask != 0U) {
        uint8_t bits = STEPPER_POWER_STATUS_ENABLE_OFF;
        if (motor_id < 8U && ((p->fault_mask >> motor_id) & 1U) != 0U) {
            bits |= STEPPER_POWER_STATUS_DRIVER_FAULT;
        }
        return bits;
    }
    if (p->host_silent) {
        return STEPPER_POWER_STATUS_ENABLE_OFF;
    }
    return 0U;
}
