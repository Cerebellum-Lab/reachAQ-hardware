/**
 * @file gpio.c
 * @brief JerryCAN Generic GPIO Message Handling
 *
 * This file provides functionality to manage and transmit generic GPIO information
 * via CAN messages through the JerryCAN library. It reads the state of each GPIO and
 * sends periodic status messages over CAN, as well as processes incoming messages to
 * set the state of specific GPIOs.
 *
 * Timer Rate: CONFIG_LIB_JERRYCAN_GPIO_TX_PERIOD_MS (in milliseconds, configurable via Kconfig)
 * - Defines the periodic rate for transmitting GPIO state messages.
 *
 * Key Functions:
 * - `jerrycan_generic_gpio_tx()`: Iterates through each GPIO instance, reads its
 *    current state, and constructs a CAN message to transmit the state.
 * - `jerrycan_generic_gpio_write_handler()`: Processes received CAN messages to
 *    set the state of a specific GPIO pin based on the command.
 * - `jerrycan_generic_gpio_pulse_handler()`: Processes received CAN messages to drive
 *    a finite, firmware-timed pulse on a specific GPIO pin.
 * - `jerrycan_generic_gpio_init()`: Initializes GPIO handling, registers callbacks
 *    for state changes, and starts the periodic transmission timer.
 *
 * Dependencies:
 * - `ll_generic_gpio_read_pin_by_name()`: Reads the state of a named GPIO pin.
 * - `ll_generic_gpio_write_pin_by_name()`: Writes a state to a named GPIO pin.
 * - `jerrycan_register_rx_callback()`: Registers a CAN message callback in the JerryCAN system.
 * - `jerrycan_tx()`: Transmits CAN messages via the JerryCAN library.
 *
 * Usage:
 * This module initializes at startup using Zephyr's SYS_INIT mechanism. It
 * registers callback functions for handling CAN messages and updates GPIO states
 * based on incoming commands. GPIO status messages are transmitted periodically,
 * and any state change on GPIOs triggers an immediate transmission.
 *
 * GPIO pulse:
 * A board opts a line in to JERRYCAN_CMD_GPIO_PULSE by naming it in the generic-gpios
 * `pulse-enable-gpio-names` property and pointing the `ll,gpio-pulse-counter` chosen
 * node at a counter instance. Both edges are driven from that counter, not from the
 * kernel tick, so the pulse width does not depend on the scheduler. A board that
 * declares neither answers every pulse command with an error rather than ignoring it.
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "generic_gpios.h"
#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DT_DRV_COMPAT ll_generic_gpios

/* Number of enabled generic gpio instances found in the device tree */
#define GENERIC_GPIO_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* A board with a counter behind this chosen node can serve JERRYCAN_CMD_GPIO_PULSE */
#define GPIO_PULSE_COUNTER_NODE DT_CHOSEN(ll_gpio_pulse_counter)
#define GPIO_PULSE_SUPPORTED DT_HAS_CHOSEN(ll_gpio_pulse_counter)

#if GPIO_PULSE_SUPPORTED
#include <zephyr/drivers/counter.h>
#endif

/* JerryCAN generic gpio context structure */
typedef struct {
    uint16_t instance_number;
    const struct device *generic_gpio;
    /* NULL-terminated list of pin names this board allows GPIO_PULSE to drive */
    const char *const *pulse_enable_names;
} jerrycan_generic_gpio_context_t;

#define PULSE_ENABLE_NAME_COMMA(node_id, prop, idx) DT_PROP_BY_IDX(node_id, prop, idx),

/*
    Per-instance list of pulse-capable pin names. A board that omits the property gets an
    empty list, so every pulse is refused unless the board opted a line in deliberately.
*/
#define PULSE_ENABLE_NAMES_DEFINE(id)                                                 \
    static const char *const pulse_enable_names_##id[] = {                            \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(id, pulse_enable_gpio_names),               \
                    (DT_INST_FOREACH_PROP_ELEM(id, pulse_enable_gpio_names,           \
                                               PULSE_ENABLE_NAME_COMMA)),             \
                    ())                                                               \
            NULL};

DT_INST_FOREACH_STATUS_OKAY(PULSE_ENABLE_NAMES_DEFINE)

#define GENERIC_GPIO_CONTEXT(id)                                                                                  \
    (jerrycan_generic_gpio_context_t) {                                                                           \
        .instance_number = id, .generic_gpio = DEVICE_DT_INST_GET(id),                                            \
        .pulse_enable_names = pulse_enable_names_##id,                                                            \
    }

#define GENERIC_GPIO_CONTEXT_COMMA(id) GENERIC_GPIO_CONTEXT(id),

/* Array of generic gpio contexts for all enabled generic gpio devices */
static const jerrycan_generic_gpio_context_t contexts[GENERIC_GPIO_COUNT] = {
    DT_INST_FOREACH_STATUS_OKAY(GENERIC_GPIO_CONTEXT_COMMA)};

static void jerrycan_generic_gpio_tx() {
    /* For each generic gpio, construct and send a GPIO message*/
    for (int i = 0; i < GENERIC_GPIO_COUNT; i++) {
        const jerrycan_generic_gpio_context_t *context = &contexts[i];
        const struct device *generic_gpio = context->generic_gpio;
        uint16_t instance_number = context->instance_number;
        uint64_t state = 0;

        int idx = 0;
        const char *pin_name = ll_generic_gpio_lookup_readable_pin_name(generic_gpio, idx);
        while (pin_name != NULL) {
            if (ll_generic_gpio_read_pin_by_name(generic_gpio, pin_name)) {
                state |= BIT(idx);
            }
            pin_name = ll_generic_gpio_lookup_readable_pin_name(generic_gpio, ++idx);
        }

        jerrycan_msg_t msg = {.type = JERRYCAN_CMD_GPIO_READ,
                              .gpio_read = {
                                  .instance = instance_number,
                                  .state = state,
                              }};

        jerrycan_tx(&msg, K_NO_WAIT);
    }
}

/* Find the context for a jerrycan instance number, or NULL if this board has no such instance */
static const jerrycan_generic_gpio_context_t *jerrycan_generic_gpio_find(uint16_t instance) {
    for (int idx = 0; idx < GENERIC_GPIO_COUNT; idx++) {
        if (contexts[idx].instance_number == instance) {
            return &contexts[idx];
        }
    }

    return NULL;
}

static int jerrycan_generic_gpio_write_handler(const jerrycan_msg_t *msg) {
    const uint16_t instance = msg->gpio_write.instance;
    const uint16_t gpio_idx = msg->gpio_write.gpio_idx;
    const bool state = msg->gpio_write.state;

    LOG_INF("Received GPIOWrite message: instance=%d, gpio_idx=%d, state=%d", instance, gpio_idx, state);

    const jerrycan_generic_gpio_context_t *context = jerrycan_generic_gpio_find(instance);

    int rc;
    if (context == NULL) {
        LOG_ERR("Failed to write GPIO over CAN: Invalid instance - %d", instance);
        rc = -ENOENT;
    } else {
        /* Grab generic gpio instance */
        const struct device *generic_gpio = context->generic_gpio;

        /* Use readable pins so that index is offset by inputs */
        const char *pin_name = ll_generic_gpio_lookup_readable_pin_name(generic_gpio, gpio_idx);
        if (pin_name == NULL) {
            LOG_ERR("Failed to write GPIO over CAN: Invalid gpio_idx: %d", gpio_idx);
            rc = -ENOENT;
        } else {
            rc = ll_generic_gpio_write_pin_by_name(generic_gpio, pin_name, state);
            if (rc != 0) {
                LOG_ERR("Failed to write GPIO over CAN: Error writing pin '%s' - %d", pin_name, rc);
            }
        }
    }

    return rc;
}

/* -------------------------------------------------------------------------- */
/* JERRYCAN_CMD_GPIO_PULSE                                                    */
/* -------------------------------------------------------------------------- */

/* The range the host already validates before it puts a pulse on the wire */
#define GPIO_PULSE_MIN_US 100U
#define GPIO_PULSE_MAX_US 5000000U

/*
    Slack allowed past the requested duration before the failsafe forces the line low.
    The failsafe only runs if the counter path failed. A stimulus line left asserted is
    worse than a pulse of the wrong length, so the line comes back down either way.
*/
#define GPIO_PULSE_FAILSAFE_MARGIN_MS 20U

static void gpio_pulse_emit_status(uint8_t instance, uint16_t gpio_idx, uint32_t duration_us, uint8_t phase,
                                   int32_t error, uint8_t uuid) {
    jerrycan_msg_t msg = {.type = JERRYCAN_CMD_GPIO_PULSE_STATUS,
                          .gpio_pulse_status =
                              {
                                  .instance = instance,
                                  .gpio_idx = gpio_idx,
                                  .duration_us = duration_us,
                                  .phase = phase,
                                  .error = error,
                              },
                          .uuid = uuid};

    jerrycan_tx(&msg, K_NO_WAIT);
}

#if GPIO_PULSE_SUPPORTED

static const struct device *const gpio_pulse_counter = DEVICE_DT_GET(GPIO_PULSE_COUNTER_NODE);

/*
    The counter has one alarm channel, so one pulse is in flight at a time, board-wide.
    Touched by the jerrycan RX thread, the counter ISR, and the failsafe work item; every
    access is under gpio_pulse_lock. generic_gpio is NULL when no pulse is armed.
*/
static struct {
    const struct device *generic_gpio;
    uint8_t writable_idx;
    uint8_t instance;
    uint16_t gpio_idx;
    uint32_t duration_us;
    uint32_t ticks_remaining;
    uint32_t last_cc;
    uint8_t uuid;
} gpio_pulse;

static struct k_spinlock gpio_pulse_lock;

static void gpio_pulse_failsafe_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(gpio_pulse_failsafe_work, gpio_pulse_failsafe_handler);

/*
    Longest single alarm. The counter driver only applies its late-setting detection to
    relative alarms shorter than half the counter range; past that a late alarm is dropped
    silently, which would strand the line asserted. Staying under half the range keeps
    every hop recoverable, and a longer pulse is served by chaining hops.
*/
static uint32_t gpio_pulse_max_hop(const struct device *dev) {
    return counter_get_top_value(dev) / 2U;
}

static uint32_t gpio_pulse_ticks_elapsed(const struct device *dev, uint32_t now, uint32_t before) {
    return (now - before) & counter_get_top_value(dev);
}

static void gpio_pulse_alarm_cb(const struct device *dev, uint8_t chan_id, uint32_t ticks, void *user_data);

static int gpio_pulse_arm(const struct device *dev, uint32_t hop_ticks) {
    const struct counter_alarm_cfg cfg = {
        .callback = gpio_pulse_alarm_cb,
        .ticks = hop_ticks,
        .user_data = NULL,
        .flags = 0, /* relative to now */
    };

    return counter_set_channel_alarm(dev, 0, &cfg);
}

/*
    Runs in the counter ISR. `ticks` is the compare value the alarm fired on, so the time
    charged against the pulse includes the latency of the previous hop's re-arm and the
    total length does not drift as hops accumulate.
*/
static void gpio_pulse_alarm_cb(const struct device *dev, uint8_t chan_id, uint32_t ticks, void *user_data) {
    ARG_UNUSED(chan_id);
    ARG_UNUSED(user_data);

    uint8_t instance = 0;
    uint16_t gpio_idx = 0;
    uint32_t duration_us = 0;
    uint8_t uuid = 0;
    uint32_t next_hop = 0;
    bool completed = false;

    k_spinlock_key_t key = k_spin_lock(&gpio_pulse_lock);

    if (gpio_pulse.generic_gpio == NULL) {
        /* The failsafe already ended this pulse */
        k_spin_unlock(&gpio_pulse_lock, key);
        return;
    }

    const uint32_t elapsed = gpio_pulse_ticks_elapsed(dev, ticks, gpio_pulse.last_cc);

    gpio_pulse.last_cc = ticks;
    gpio_pulse.ticks_remaining = (elapsed >= gpio_pulse.ticks_remaining) ? 0 : gpio_pulse.ticks_remaining - elapsed;

    if (gpio_pulse.ticks_remaining == 0) {
        (void)ll_generic_gpio_write_pin(gpio_pulse.generic_gpio, gpio_pulse.writable_idx, 0);

        instance = gpio_pulse.instance;
        gpio_idx = gpio_pulse.gpio_idx;
        duration_us = gpio_pulse.duration_us;
        uuid = gpio_pulse.uuid;
        completed = true;

        gpio_pulse.generic_gpio = NULL;
        (void)k_work_cancel_delayable(&gpio_pulse_failsafe_work);
    } else {
        next_hop = MIN(gpio_pulse.ticks_remaining, gpio_pulse_max_hop(dev));
        gpio_idx = gpio_pulse.gpio_idx;
    }

    k_spin_unlock(&gpio_pulse_lock, key);

    if (completed) {
        jerrycan_generic_gpio_tx();
        gpio_pulse_emit_status(instance, gpio_idx, duration_us, JERRYCAN_GPIO_PULSE_PHASE_COMPLETED, 0, uuid);
        return;
    }

    const int rc = gpio_pulse_arm(dev, next_hop);
    if (rc != 0) {
        /* The failsafe is still scheduled and will bring the line back down */
        LOG_ERR("Failed to continue GPIO pulse on gpio_idx %d: %d", gpio_idx, rc);
    }
}

/* Forces the line low if the counter path did not. Not expected to ever run. */
static void gpio_pulse_failsafe_handler(struct k_work *work) {
    ARG_UNUSED(work);

    uint8_t instance;
    uint16_t gpio_idx;
    uint32_t duration_us;
    uint8_t uuid;

    k_spinlock_key_t key = k_spin_lock(&gpio_pulse_lock);

    if (gpio_pulse.generic_gpio == NULL) {
        k_spin_unlock(&gpio_pulse_lock, key);
        return;
    }

    (void)counter_cancel_channel_alarm(gpio_pulse_counter, 0);
    (void)ll_generic_gpio_write_pin(gpio_pulse.generic_gpio, gpio_pulse.writable_idx, 0);

    instance = gpio_pulse.instance;
    gpio_idx = gpio_pulse.gpio_idx;
    duration_us = gpio_pulse.duration_us;
    uuid = gpio_pulse.uuid;

    gpio_pulse.generic_gpio = NULL;

    k_spin_unlock(&gpio_pulse_lock, key);

    LOG_ERR("GPIO pulse failsafe forced gpio_idx %d low after %u us", gpio_idx, duration_us);

    jerrycan_generic_gpio_tx();
    gpio_pulse_emit_status(instance, gpio_idx, duration_us, JERRYCAN_GPIO_PULSE_PHASE_COMPLETED, -ETIME, uuid);
}

/* True when the board named this pin in pulse-enable-gpio-names */
static bool gpio_pulse_pin_enabled(const jerrycan_generic_gpio_context_t *context, const char *pin_name) {
    for (const char *const *name = context->pulse_enable_names; *name != NULL; name++) {
        if (strcmp(*name, pin_name) == 0) {
            return true;
        }
    }

    return false;
}

/*
    Map a readable pin name onto its writable pin index. Resolved once, before the line is
    asserted, so both edges are a single register write with no name lookup in between.
*/
static int gpio_pulse_writable_idx(const struct device *generic_gpio, const char *pin_name) {
    for (int idx = 0;; idx++) {
        const char *name = ll_generic_gpio_lookup_writable_pin_name(generic_gpio, idx);
        if (name == NULL) {
            return -EOPNOTSUPP;
        }
        if (strcmp(name, pin_name) == 0) {
            return idx;
        }
    }
}

static int gpio_pulse_start(uint16_t instance, uint16_t gpio_idx, uint32_t duration_us, uint8_t uuid) {
    const jerrycan_generic_gpio_context_t *context = jerrycan_generic_gpio_find(instance);
    if (context == NULL) {
        LOG_ERR("Failed to pulse GPIO over CAN: Invalid instance - %d", instance);
        return -ENOENT;
    }

    const struct device *generic_gpio = context->generic_gpio;

    /* Use readable pins so that index is offset by inputs */
    const char *pin_name = ll_generic_gpio_lookup_readable_pin_name(generic_gpio, gpio_idx);
    if (pin_name == NULL) {
        LOG_ERR("Failed to pulse GPIO over CAN: Invalid gpio_idx: %d", gpio_idx);
        return -ENOENT;
    }

    if (!gpio_pulse_pin_enabled(context, pin_name)) {
        LOG_ERR("Failed to pulse GPIO over CAN: Pin '%s' is not pulse capable", pin_name);
        return -EPERM;
    }

    if (duration_us < GPIO_PULSE_MIN_US || duration_us > GPIO_PULSE_MAX_US) {
        LOG_ERR("Failed to pulse GPIO over CAN: Duration %u us is out of range", duration_us);
        return -EINVAL;
    }

    const int writable_idx = gpio_pulse_writable_idx(generic_gpio, pin_name);
    if (writable_idx < 0) {
        LOG_ERR("Failed to pulse GPIO over CAN: Pin '%s' is not writable", pin_name);
        return writable_idx;
    }

    if (!device_is_ready(gpio_pulse_counter)) {
        LOG_ERR("Failed to pulse GPIO over CAN: The pulse counter is not ready");
        return -ENODEV;
    }

    const uint32_t total_ticks = counter_us_to_ticks(gpio_pulse_counter, duration_us);
    if (total_ticks == 0) {
        LOG_ERR("Failed to pulse GPIO over CAN: The counter cannot resolve %u us", duration_us);
        return -EINVAL;
    }

    k_spinlock_key_t key = k_spin_lock(&gpio_pulse_lock);

    if (gpio_pulse.generic_gpio != NULL) {
        k_spin_unlock(&gpio_pulse_lock, key);
        LOG_ERR("Failed to pulse GPIO over CAN: A pulse is already in flight");
        return -EBUSY;
    }

    gpio_pulse.generic_gpio = generic_gpio;
    gpio_pulse.writable_idx = (uint8_t)writable_idx;
    gpio_pulse.instance = (uint8_t)instance;
    gpio_pulse.gpio_idx = gpio_idx;
    gpio_pulse.duration_us = duration_us;
    gpio_pulse.ticks_remaining = total_ticks;
    gpio_pulse.uuid = uuid;

    /*
        Arm the failsafe before the line moves, so there is never a moment where the line
        is asserted with nothing scheduled to bring it back down.
    */
    (void)k_work_schedule(&gpio_pulse_failsafe_work,
                          K_MSEC(duration_us / USEC_PER_MSEC + GPIO_PULSE_FAILSAFE_MARGIN_MS));

    int rc = ll_generic_gpio_write_pin(generic_gpio, writable_idx, 1);
    if (rc == 0) {
        uint32_t now = 0;

        (void)counter_get_value(gpio_pulse_counter, &now);
        gpio_pulse.last_cc = now;

        rc = gpio_pulse_arm(gpio_pulse_counter, MIN(total_ticks, gpio_pulse_max_hop(gpio_pulse_counter)));
    }

    if (rc != 0) {
        (void)ll_generic_gpio_write_pin(generic_gpio, writable_idx, 0);
        gpio_pulse.generic_gpio = NULL;
        (void)k_work_cancel_delayable(&gpio_pulse_failsafe_work);
    }

    k_spin_unlock(&gpio_pulse_lock, key);

    if (rc != 0) {
        LOG_ERR("Failed to pulse GPIO over CAN: Error asserting pin '%s' - %d", pin_name, rc);
    }

    return rc;
}

#else /* !GPIO_PULSE_SUPPORTED */

static int gpio_pulse_start(uint16_t instance, uint16_t gpio_idx, uint32_t duration_us, uint8_t uuid) {
    ARG_UNUSED(instance);
    ARG_UNUSED(gpio_idx);
    ARG_UNUSED(duration_us);
    ARG_UNUSED(uuid);

    LOG_ERR("Failed to pulse GPIO over CAN: This board has no gpio pulse counter");

    return -ENOTSUP;
}

#endif /* GPIO_PULSE_SUPPORTED */

static int jerrycan_generic_gpio_pulse_handler(const jerrycan_msg_t *msg) {
    const uint16_t instance = msg->gpio_pulse.instance;
    const uint16_t gpio_idx = msg->gpio_pulse.gpio_idx;
    const uint32_t duration_us = msg->gpio_pulse.duration_us;
    const uint8_t uuid = msg->uuid;

    LOG_INF("Received GPIOPulse message: instance=%d, gpio_idx=%d, duration_us=%u", instance, gpio_idx, duration_us);

    const int rc = gpio_pulse_start(instance, gpio_idx, duration_us, uuid);
    if (rc != 0) {
        gpio_pulse_emit_status(instance, gpio_idx, duration_us, JERRYCAN_GPIO_PULSE_PHASE_REJECTED, rc, uuid);
    } else {
        gpio_pulse_emit_status(instance, gpio_idx, duration_us, JERRYCAN_GPIO_PULSE_PHASE_ASSERTED, 0, uuid);
        jerrycan_generic_gpio_tx();
    }

    /* This return code becomes the acknowledgement, so a rejection is still acknowledged */
    return rc;
}

static jerrycan_rx_callback_t gpio_callback = {
    .filter_msg_type = JERRYCAN_CMD_GPIO_WRITE,
    .func = jerrycan_generic_gpio_write_handler,
};

static jerrycan_rx_callback_t gpio_pulse_callback = {
    .filter_msg_type = JERRYCAN_CMD_GPIO_PULSE,
    .func = jerrycan_generic_gpio_pulse_handler,
};

K_TIMER_DEFINE(jerrycan_generic_gpio_timer, jerrycan_generic_gpio_tx, NULL);

static int jerrycan_generic_gpio_init() {
    /* Register generic gpio state change callback (send message immediately on state change) */
    for (int i = 0; i < GENERIC_GPIO_COUNT; i++) {
        ll_generic_gpio_register_state_change_handler(contexts[i].generic_gpio, jerrycan_generic_gpio_tx);
    }

    /* Register gpio Rx callbacks */
    jerrycan_register_rx_callback(&gpio_callback);
    jerrycan_register_rx_callback(&gpio_pulse_callback);

#if GPIO_PULSE_SUPPORTED
    /* Free-run the counter that times the pulse return-low edge */
    if (device_is_ready(gpio_pulse_counter)) {
        const int rc = counter_start(gpio_pulse_counter);
        if (rc != 0) {
            LOG_ERR("Failed to start the gpio pulse counter: %d", rc);
        }
    } else {
        LOG_ERR("The gpio pulse counter is not ready");
    }
#endif

    /* Start timer to send the gpio messages periodically */
    k_timer_start(&jerrycan_generic_gpio_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_GPIO_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(jerrycan_generic_gpio_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
