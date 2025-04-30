/**
 * @file pressure.c
 * @brief JerryCAN Pressure Sensor Message Handling
 *
 * This file provide functionality to manage and transmit pressure sensor information
 * via CAN messages through the JerryCAN library. It periodically reads the state and
 * pressure value from each enabled pressure sensor and sends the information as a
 * CAN message. This provides continuous monitoring of real-time pressure sensor data.
 *
 * Timer Rate: CONFIG_LIB_JERRYCAN_PRESSURE_TX_PERIOD_MS (in milliseconds, configurable via Kconfig)
 * - Sets the periodic rate for reading and transmitting pressure sensor data over CAN.
 *
 * Key Functions:
 * - `jerrycan_pressure_sensor_tx()`: Iterates through each pressure sensor instance,
 *    collects its current state and pressure measurement, and sends the data as a
 *    CAN message.
 * - `jerrycan_pressure_sensor_init()`: Initializes the pressure sensor module and
 *    starts a timer to trigger `jerrycan_pressure_sensor_tx()` at the configured rate.
 *
 * Dependencies:
 * - `ll_pressure_sensor_is_initialized()`: Checks if the pressure sensor is initialized.
 * - `ll_pressure_sensor_is_enabled()`: Checks if the pressure sensor is enabled.
 * - `ll_pressure_sensor_get_pressure()`: Retrieves the current pressure value (in mV) from the sensor.
 * - `jerrycan_tx()`: Transmits CAN messages via the JerryCAN library.
 *
 * Usage:
 * This module initializes at startup using Zephyr’s SYS_INIT macro. The timer is
 * started to periodically transmit pressure sensor data over CAN, with the rate
 * controlled by `CONFIG_LIB_JERRYCAN_PRESSURE_TX_PERIOD_MS`. Each enabled pressure sensor
 * instance is checked for initialization and enabled status before transmitting its
 * pressure value and state information.
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"
#include "pressure_sensor.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DT_DRV_COMPAT ll_pressure_sensor

/* Number of enabled pressure sensors found in the device tree */
#define PRESSURE_SENSOR_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* JerryCAN pressure sensor tx context structure */
typedef struct {
    uint16_t instance_number;
    const struct device *pressure_sensor;
} jerrycan_pressure_sensor_context_t;

#define JERRYCAN_PRESSURE_SENSOR_CONTEXT(id) \
    (jerrycan_pressure_sensor_context_t) { .instance_number = id, .pressure_sensor = DEVICE_DT_INST_GET(id), }

#define JERRYCAN_PRESSURE_SENSOR_CONTEXT_COMMA(id) JERRYCAN_PRESSURE_SENSOR_CONTEXT(id),

/* Array of pressure sensor tx contexts for all enabled pressure sensor devices */
static const jerrycan_pressure_sensor_context_t contexts[PRESSURE_SENSOR_COUNT] = {
    DT_INST_FOREACH_STATUS_OKAY(JERRYCAN_PRESSURE_SENSOR_CONTEXT_COMMA)};

static void jerrycan_pressure_sensor_tx() {
    /* For each pressure sensor, construct and send a pressure message*/
    for (int i = 0; i < PRESSURE_SENSOR_COUNT; i++) {
        const jerrycan_pressure_sensor_context_t *context = &contexts[i];
        const struct device *pressure_sensor = context->pressure_sensor;
        uint16_t instance_number = context->instance_number;

        /* Local variable for storing pressure value */
        uint16_t pressure_mv = 0;
        jerrycan_msg_t msg = {.type = JERRYCAN_CMD_PRESSURE_READ,
                              .pressure_read = {
                                  .instance = instance_number,
                                  .error = ll_pressure_sensor_get_pressure(pressure_sensor, &pressure_mv),
                                  .pressure_mv = pressure_mv,
                              }};

        /* Transmit message */
        int ret = jerrycan_tx(&msg, K_NO_WAIT);
        if (ret != 0) {
            LOG_ERR("Error sending CAN message for pressure_sensor%d: %d", context->instance_number, ret);
        }
    }
}

static int jerrycan_pressure_sensor_tare_handler(const jerrycan_msg_t *msg) {
    int rc = -ENOENT;

    for (int i = 0; i < PRESSURE_SENSOR_COUNT; i++) {
        const jerrycan_pressure_sensor_context_t *context = &contexts[i];
        const struct device *pressure_sensor = context->pressure_sensor;
        uint16_t instance_number = context->instance_number;

        if (msg->pressure_sensor_tare.instance == instance_number) {
            rc = ll_pressure_sensor_tare(pressure_sensor);
            if (rc < 0) {
                LOG_ERR("Failed to perform the requested pressure sensor tare operation: %d", rc);
            } else {
                LOG_INF("Successfully tared pressure_sensor%d", instance_number);
            }
            break;
        }
    }

    return rc;
}

static jerrycan_rx_callback_t pressure_sensor_tare_callback = {
    .filter_msg_type = JERRYCAN_CMD_PRESSURE_SENSOR_TARE,
    .func = jerrycan_pressure_sensor_tare_handler,
};

K_TIMER_DEFINE(jerrycan_pressure_sensor_timer, jerrycan_pressure_sensor_tx, NULL);

static int jerrycan_pressure_sensor_init() {
    jerrycan_register_rx_callback(&pressure_sensor_tare_callback);

    /* Start timer to send the status messages periodically */
    k_timer_start(&jerrycan_pressure_sensor_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_PRESSURE_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(jerrycan_pressure_sensor_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
