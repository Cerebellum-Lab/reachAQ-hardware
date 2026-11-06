#include "stepper_power.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "adi_tmc2209.h"
#include "jerrycan.h"
#include "motor_common.h"
#include "motor_motion.h"
#include "stepper_power_policy.h"

LOG_MODULE_DECLARE(motor_motion, CONFIG_LIB_MOTOR_MOTION_LOG_LEVEL);

#define STEPPER_POWER_NODE DT_INST(0, ll_stepper_power)

// The datasheet (rev 1.09) states no delay from ENN low to the first step, so this is a margin.
#define STEPPER_POWER_ENABLE_SETTLE_US 1000U
#define STEPPER_POWER_DETECT_SETTLE_US 20U

static const struct gpio_dt_spec enable = GPIO_DT_SPEC_GET(STEPPER_POWER_NODE, enable_gpios);
static const struct gpio_dt_spec diag[] = {
    DT_FOREACH_PROP_ELEM_SEP(STEPPER_POWER_NODE, diag_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, ))};

// DIAG index equals motor id.
BUILD_ASSERT(ARRAY_SIZE(diag) == DT_NUM_INST_STATUS_OKAY(ll_stepper), "One DIAG line per stepper");

// Taken from the main thread, the motor work queue and the status timer's ISR. Never held across the UART.
static struct k_spinlock lock;
static stepper_power_policy_t policy;
static bool enable_on;

static const struct device *driver_by_id(const size_t id) {
    const ll_motor_cfg_t *cfg = stepper_motor_by_id(id)->config;

    return cfg->stepper_driver_device;
}

/**
 * Thread context only: the UART sleeps. Checks and logs every driver, not just the first failure.
 *
 * @return true if all of them proved configured.
 */
static bool drivers_configured(void) {
    bool all_configured = true;

    for (size_t i = 0; i < ARRAY_SIZE(diag); i++) {
        const int ret = adi_tmc2209_verify_configured(driver_by_id(i));
        if (ret != 0) {
            LOG_ERR("Stepper driver %d not proven configured: %d", (int)i, ret);
            all_configured = false;
        }
    }

    return all_configured;
}

static uint8_t read_diag(void) {
    uint8_t mask = 0U;

    for (size_t i = 0; i < ARRAY_SIZE(diag); i++) {
        if (gpio_pin_get_dt(&diag[i]) == 1) {
            mask |= BIT(i);
        }
    }

    return mask;
}

// Write EN to what the policy wants. With no enable net this never touches PD2.
static void apply(void) {
    bool turned_on = false;

    k_spinlock_key_t key = k_spin_lock(&lock);
    if (policy.has_enable) {
        const bool wanted = stepper_power_policy_enable_wanted(&policy);

        gpio_pin_set_dt(&enable, wanted);
        turned_on = wanted && !enable_on;
        enable_on = wanted;
    }
    k_spin_unlock(&lock, key);

    if (turned_on) {
        k_busy_wait(STEPPER_POWER_ENABLE_SETTLE_US);
    }
}

void stepper_power_init(void) {
    gpio_pin_configure_dt(&enable, GPIO_INPUT | GPIO_PULL_DOWN);
    k_busy_wait(STEPPER_POWER_DETECT_SETTLE_US);

    if (gpio_pin_get_raw(enable.port, enable.pin) != 1) {
        gpio_pin_configure_dt(&enable, GPIO_DISCONNECTED);
        LOG_INF("no stepper enable net");
        return;
    }

    // From here a failure leaves the drivers unproven, so EN stays off and motion is refused.
    k_spinlock_key_t key = k_spin_lock(&lock);
    stepper_power_policy_init(&policy, true);
    k_spin_unlock(&lock, key);

    int ret = gpio_pin_configure_dt(&enable, GPIO_OUTPUT_INACTIVE);
    for (size_t i = 0; ret == 0 && i < ARRAY_SIZE(diag); i++) {
        ret = gpio_pin_configure_dt(&diag[i], GPIO_INPUT);
    }
    if (ret != 0) {
        LOG_ERR("Failed (%d) to configure the stepper enable or DIAG pins", ret);
        return;
    }

    if (drivers_configured()) {
        key = k_spin_lock(&lock);
        stepper_power_policy_set_ready(&policy, true, k_uptime_get_32());
        k_spin_unlock(&lock, key);
    }

    apply();

    jerrycan_set_host_frame_hook(stepper_power_host_frame);
}

void stepper_power_poll(void) {
    const uint8_t diag_mask = read_diag();

    // `now` is sampled inside the lock: one older than a newer last_host_ms would wrap to a false silence.
    k_spinlock_key_t key = k_spin_lock(&lock);
    const stepper_power_action_t action =
        stepper_power_policy_poll(&policy, diag_mask, k_uptime_get_32(), CONFIG_LIB_MOTOR_MOTION_HOST_SILENCE_TIMEOUT_MS);
    const uint8_t faulted = policy.fault_mask;
    k_spin_unlock(&lock, key);

    switch (action) {
        case STEPPER_POWER_ACT_STOP_FAULT:
            steppers_stop_moving();
            for (size_t i = 0; i < ARRAY_SIZE(diag); i++) {
                if (faulted & BIT(i)) {
                    adi_tmc2209_log_fault(driver_by_id(i));
                }
            }
            set_all_e_stop_flags();
            break;

        case STEPPER_POWER_ACT_STOP_SILENT:
            stepper_mark_unhomed(steppers_stop_moving());
            LOG_WRN("No host frame for %d ms: stepper enable off", CONFIG_LIB_MOTOR_MOTION_HOST_SILENCE_TIMEOUT_MS);
            break;

        default:
            break;
    }

    // Last: DRV_STATUS must be read before EN off clears it.
    apply();
}

void stepper_power_host_frame(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool was_silent = policy.host_silent;
    stepper_power_policy_host_frame(&policy, k_uptime_get_32());
    k_spin_unlock(&lock, key);

    // Nothing else a frame does changes what EN should be, and a write here could take EN off while the poll is
    // still reading DRV_STATUS after a fault.
    if (was_silent) {
        apply();
    }
}

void stepper_power_set_estop(const bool asserted) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    stepper_power_policy_set_estop(&policy, asserted);
    k_spin_unlock(&lock, key);

    if (asserted) {
        trigger_e_stop();
    }

    apply();
}

bool stepper_power_estop_active(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool active = policy.estop;
    k_spin_unlock(&lock, key);

    return active;
}

int stepper_power_motion_error(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const int rc = stepper_power_policy_motion_error(&policy);
    k_spin_unlock(&lock, key);

    return rc;
}

int stepper_power_recover(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool reprove = policy.fault_mask != 0U && !policy.estop;
    k_spin_unlock(&lock, key);

    // DIAG also fires on a driver power-on reset, which leaves that driver at IRUN=31.
    const bool proven = !reprove || drivers_configured();

    key = k_spin_lock(&lock);
    if (!proven) {
        stepper_power_policy_set_ready(&policy, false, k_uptime_get_32());
    }
    const int rc = stepper_power_policy_recover(&policy);
    k_spin_unlock(&lock, key);

    apply();

    return rc;
}

uint8_t stepper_power_status_bits(const uint8_t motor_id) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const uint8_t bits = stepper_power_policy_status_bits(&policy, motor_id);
    k_spin_unlock(&lock, key);

    return bits;
}

bool stepper_power_present(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    const bool present = policy.has_enable;
    k_spin_unlock(&lock, key);

    return present;
}

void stepper_power_force_off_for_fault(void) {
    if (policy.has_enable) {
        gpio_pin_set_dt(&enable, 0);
        enable_on = false;
    }
}

void stepper_power_read_state(stepper_power_state_t *state) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    state->detected = policy.has_enable;
    state->ready = policy.drivers_ready;
    state->silent = policy.host_silent;
    state->estop = policy.estop;
    state->enable_on = enable_on;
    state->fault_mask = policy.fault_mask;
    k_spin_unlock(&lock, key);

    state->diag_mask = state->detected ? read_diag() : 0U;
}
