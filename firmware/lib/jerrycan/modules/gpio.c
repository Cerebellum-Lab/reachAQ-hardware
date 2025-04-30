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
 * This module initializes at startup using Zephyr’s SYS_INIT mechanism. It
 * registers callback functions for handling CAN messages and updates GPIO states
 * based on incoming commands. GPIO status messages are transmitted periodically,
 * and any state change on GPIOs triggers an immediate transmission.
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "generic_gpios.h"
#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DT_DRV_COMPAT ll_generic_gpios

/* Number of enabled generic gpio instances found in the device tree */
#define GENERIC_GPIO_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* JerryCAN generic gpio context structure */
typedef struct {
    uint16_t instance_number;
    const struct device *generic_gpio;
} jerrycan_generic_gpio_context_t;

#define GENERIC_GPIO_CONTEXT(id) \
    (jerrycan_generic_gpio_context_t) { .instance_number = id, .generic_gpio = DEVICE_DT_INST_GET(id), }

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

static int jerrycan_generic_gpio_write_handler(const jerrycan_msg_t *msg) {
    const uint16_t instance = msg->gpio_write.instance;
    const uint16_t gpio_idx = msg->gpio_write.gpio_idx;
    const bool state = msg->gpio_write.state;

    LOG_INF("Received GPIOWrite message: instance=%d, gpio_idx=%d, state=%d", instance, gpio_idx, state);

    int idx = 0;
    while (idx < GENERIC_GPIO_COUNT && contexts[idx].instance_number != instance) {
        idx++;
    }

    int rc;
    if (idx >= GENERIC_GPIO_COUNT) {
        LOG_ERR("Failed to write GPIO over CAN: Invalid instance - %d", instance);
        rc = -ENOENT;
    } else {
        /* Grab generic gpio instance */
        const struct device *generic_gpio = contexts[idx].generic_gpio;

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

static jerrycan_rx_callback_t gpio_callback = {
    .filter_msg_type = JERRYCAN_CMD_GPIO_WRITE,
    .func = jerrycan_generic_gpio_write_handler,
};

K_TIMER_DEFINE(jerrycan_generic_gpio_timer, jerrycan_generic_gpio_tx, NULL);

static int jerrycan_generic_gpio_init() {
    /* Register generic gpio state change callback (send message immediately on state change) */
    for (int i = 0; i < GENERIC_GPIO_COUNT; i++) {
        ll_generic_gpio_register_state_change_handler(contexts[i].generic_gpio, jerrycan_generic_gpio_tx);
    }

    /* Register gpio Rx callback */
    jerrycan_register_rx_callback(&gpio_callback);

    /* Start timer to send the gpio messages periodically */
    k_timer_start(&jerrycan_generic_gpio_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_GPIO_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(jerrycan_generic_gpio_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
