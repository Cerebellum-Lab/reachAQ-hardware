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
 * - `jerrycan_analog_out_tx()`: Transmits the current voltage value for each
 *    analog output instance.
 * - `jerrycan_analog_out_write_handler()`: Processes CAN messages to update analog
 *    status values based on received commands.
 * - `jerrycan_analog_out_init()`: Initializes the analog output module, registers
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

LOG_MODULE_DECLARE(jerrycan, LOG_LEVEL_DBG);

#define DT_DRV_COMPAT ll_analog_out

/* Number of enabled analog output instances found in the device tree */
#define ANALOG_OUT_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* JerryCAN analog output context structure */
typedef struct {
    uint16_t instance_number;
    const struct device *analog_out;
} jerrycan_analog_out_context_t;

#define ANALOG_OUT_CONTEXT(id) \
    (jerrycan_analog_out_context_t) { .instance_number = id, .analog_out = DEVICE_DT_INST_GET(id), }

#define ANALOG_OUT_CONTEXT_COMMA(id) ANALOG_OUT_CONTEXT(id),

/* Array of analog output contexts for all enabled analog output devices */
static const jerrycan_analog_out_context_t contexts[ANALOG_OUT_COUNT] = {
    DT_INST_FOREACH_STATUS_OKAY(ANALOG_OUT_CONTEXT_COMMA)};

static void jerrycan_analog_out_tx() {
    /* For each analog output, construct and send an analog output message*/
    for (int i = 0; i < ANALOG_OUT_COUNT; i++) {
        const jerrycan_analog_out_context_t *context = &contexts[i];
        const struct device *analog_out = context->analog_out;
        uint16_t instance_number = context->instance_number;

        jerrycan_msg_t msg = {.type = JERRYCAN_CMD_ANALOG_OUT,
                              .analog_out = {
                                  .instance = instance_number,
                                  .value_mv = ll_analog_out_get_value_mv(analog_out),
                              }};

        jerrycan_tx(&msg, K_NO_WAIT);
    }
}

static void jerrycan_analog_out_write_handler(jerrycan_msg_t *msg) {
    /* Pull parameters from jerrycan message */
    uint16_t instance = msg->analog_out.instance;
    uint16_t value_mv = msg->analog_out.value_mv;

    LOG_DBG("Recieved analog output write value message: instance=%d, value_mv=%d", instance, value_mv);

    int idx = 0;
    while (idx < ANALOG_OUT_COUNT && contexts[idx].instance_number != instance) {
        idx++;
    }

    if (idx >= ANALOG_OUT_COUNT) {
        LOG_ERR("Failed to write analog output over CAN: Invalid instance - %d", instance);
        return;
    }

    /* Grab analog output instance */
    const struct device *analog_out = contexts[idx].analog_out;

    /* Write value to analog output, printing error on failure */
    int ret = ll_analog_out_write_value_mv(analog_out, value_mv);
    if (ret != ANALOG_OUT_NO_ERROR) {
        LOG_ERR("Failed to write analog output value over CAN: Error writing value - %s", analog_out_error_to_str[ret]);
    }
}

static jerrycan_rx_callback_t analog_out_callback = {
    .filter_msg_type = JERRYCAN_CMD_ANALOG_OUT,
    .func = jerrycan_analog_out_write_handler,
};

K_TIMER_DEFINE(jerrycan_analog_out_timer, jerrycan_analog_out_tx, NULL);

static int jerrycan_analog_out_init() {
    int ret = jerrycan_register_rx_callback(&analog_out_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register analog output callback: %d", ret);
    }

    /* Start timer to send the status messages periodically */
    k_timer_start(&jerrycan_analog_out_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_ANALOG_OUT_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(jerrycan_analog_out_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
