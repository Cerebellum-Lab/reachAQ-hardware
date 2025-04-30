/**
 * @file analog.c
 * @brief JerryCAN Analog Out Message Handling
 *
 * This file provides functionality to manage and transmit analog output information
 * via CAN messages through the JerryCAN library. Each enabled analog output instance
 * periodically transmits its current out voltage in millivolts as CAN messages. This
 * file also handles received messages for setting analog output values.
 *
 * Timer Rate: CONFIG_LIB_JERRYCAN_ANALOG_OUT_TX_PERIOD_MS (in milliseconds, set via Kconfig)
 * - Controls the periodic transmission rate of analog output messages.
 *
 * Key Functions:
 * - `transmit_status()`: Transmits the current voltage value for each
 *    analog output instance.
 * - `set_channel()`: Processes CAN messages to update analog
 *    status values based on received commands.
 * - `initialize()`: Initializes the analog output module, registers
 *    the CAN message callback, and starts the periodic timer for transmission.
 *
 * Dependencies:
 * - `ll_analog_out_get_value_mv()`: Retrieves the measured value (in mV) of each
 *    analog output device.
 * - `ll_analog_out_write_value_mv()`: Writes a new value (in mV) to an analog
 *    status instance based on a received message.
 * - `jerrycan_register_rx_callback()`: Registers a CAN message callback in the JerryCAN system.
 * - `jerrycan_tx()`: Transmits CAN messages via the JerryCAN library.
 *
 * Usage:
 * This module is initialized automatically at startup using Zephyr’s SYS_INIT macro,
 * and it will start periodically transmitting analog output data over CAN. The periodic
 * rate can be configured through Kconfig.
 */

#include <zephyr/logging/log.h>

#include "analog_out.h"
#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DT_DRV_COMPAT ll_analog_out

/* Number of enabled analog output instances found in the device tree */
#define ANALOG_OUT_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* JerryCAN analog output context structure */
typedef struct {
    uint16_t instance_number;
    const struct device *analog_out;
} context_t;

#define ANALOG_OUT_CONTEXT(id) \
    (context_t) { .instance_number = id, .analog_out = DEVICE_DT_INST_GET(id), }

#define ANALOG_OUT_CONTEXT_COMMA(id) ANALOG_OUT_CONTEXT(id),

/* Array of analog output contexts for all enabled analog output devices */
static const context_t contexts[ANALOG_OUT_COUNT] = {DT_INST_FOREACH_STATUS_OKAY(ANALOG_OUT_CONTEXT_COMMA)};

/* -------------------------------------------------------------------------- */

static void transmit_status() {
    /* For each analog output, construct and send an analog output message*/
    for (int i = 0; i < ANALOG_OUT_COUNT; i++) {
        const context_t *context = &contexts[i];
        const struct device *analog_out = context->analog_out;

        jerrycan_msg_t msg = {.type = JERRYCAN_CMD_ANALOG_OUT,
                              .analog_out = {
                                  .instance = context->instance_number,
                                  .value_mv = ll_analog_out_get_value_mv(analog_out),
                              }};

        jerrycan_tx(&msg, K_NO_WAIT);
    }
}

/* -------------------------------------------------------------------------- */

static int set_channel(const jerrycan_msg_t *msg) {
    const uint16_t instance = msg->analog_out.instance;
    const uint16_t value_mv = msg->analog_out.value_mv;

    LOG_DBG("Recieved analog output write value message: instance=%d, value_mv=%d", instance, value_mv);

    int idx = 0;
    while (idx < ANALOG_OUT_COUNT && contexts[idx].instance_number != instance) {
        idx++;
    }

    if (idx >= ANALOG_OUT_COUNT) {
        LOG_ERR("Failed to write analog output over CAN: Invalid instance - %d", instance);
        return -ENOENT;
    }

    /* Grab analog output instance */
    const struct device *analog_out = contexts[idx].analog_out;

    /* Write value to analog output, printing error on failure */
    const int ret = ll_analog_out_write_value_mv(analog_out, value_mv);
    if (ret != ANALOG_OUT_NO_ERROR) {
        LOG_ERR("Failed to write analog output value over CAN: Error writing value - %s", analog_out_error_to_str[ret]);
    }

    return -ret;
}

/* -------------------------------------------------------------------------- */

static K_TIMER_DEFINE(status_timer, transmit_status, NULL);

static int initialize() {
    static jerrycan_rx_callback_t set_channel_callback = {
        .filter_msg_type = JERRYCAN_CMD_ANALOG_OUT,
        .func = set_channel,
    };

    jerrycan_register_rx_callback(&set_channel_callback);

    k_timer_start(&status_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_ANALOG_OUT_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(initialize, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
