/**
 * @file temperature.c
 * @brief JerryCAN Temperature and Humidity Sensor Message Handling
 *
 * This file provides functionality to manage and transmit temperature and humidity sensor
 * data via CAN messages through the JerryCAN library. Each temperature/humidity sensor
 * instance is periodically read, and the resulting measurements are sent as a CAN message.
 * It handles temperature and humidity measurement for all enabled temperature sensors and
 * ensures these are sent without blocking the ISR by using a work queue.
 *
 * Timer Rate: CONFIG_LIB_JERRYCAN_TEMPERATURE_TX_PERIOD_MS (in milliseconds, configurable via Kconfig)
 * - Determines how often temperature and humidity data are fetched and transmitted.
 *
 * Key Functions:
 * - `jerrycan_temperature_sensor_tx_handler()`: Fetches temperature and humidity readings
 *    from each sensor, constructs a CAN message, and transmits the data. This function
 *    is queued in the work queue due to the I2C transaction.
 * - `jerrycan_temperature_sensor_tx()`: Submits the temperature and humidity fetch
 *    job to the system work queue, avoiding I2C communication in the ISR.
 * - `jerrycan_temperature_sensor_init()`: Initializes the temperature sensor module
 *    and starts a periodic timer to trigger `jerrycan_temperature_sensor_tx()` based
 *    on the configured rate.
 *
 * Dependencies:
 * - `sensor_sample_fetch()`: Fetches data from the temperature sensor.
 * - `sensor_channel_get()`: Retrieves specific channel data (temperature or humidity)
 *    from the fetched sample.
 * - `jerrycan_tx()`: Transmits CAN messages via the JerryCAN library.
 *
 * Usage:
 * This module initializes at startup using Zephyr’s SYS_INIT macro. A timer periodically
 * triggers a fetch operation for temperature and humidity data, submitting this job
 * to the work queue. The timer rate can be set from the Kconfig with
 * `CONFIG_LIB_JERRYCAN_TEMPERATURE_TX_PERIOD_MS`. The message includes temperature and humidity
 * values in hundredths (e.g., 25.75°C would be transmitted as 2575) for improved resolution.
 * This can be done because the operating range of the sensor lies below two orders of magnitude
 * of the maximum value of the data type used to transmit them.
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, LOG_LEVEL_DBG);

#define DT_DRV_COMPAT silabs_si7006

/* Number of enabled temperature sensors found in the device tree */
#define TEMPERATURE_SENSOR_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* JerryCAN temperature sensor tx context structure */
typedef struct {
    uint16_t instance_number;
    const struct device *temperature_sensor;
} jerrycan_temperature_sensor_tx_context_t;

#define TEMPERATURE_SENSOR_TX_CONTEXT(id) \
    (jerrycan_temperature_sensor_tx_context_t) { .instance_number = id, .temperature_sensor = DEVICE_DT_INST_GET(id), }

#define TEMPERATURE_SENSOR_TX_CONTEXT_COMMA(id) TEMPERATURE_SENSOR_TX_CONTEXT(id),

/* Maps k_work_submit errors to strings */
static inline const char *k_work_submit_error_to_str(int errno) {
    switch (errno) {
        case EBUSY:
            return "work item is cancelling; or queue is draining; or queue is plugged";
        case EINVAL:
            return "queue is null and the work item has never been run";
        case ENODEV:
            return "queue has not been started";
        default:
            return "unknown error";
    }
}

/* Array of temperature sensor tx contexts for all enabled temperature sensor devices */
static const jerrycan_temperature_sensor_tx_context_t tx_contexts[TEMPERATURE_SENSOR_COUNT] = {
    DT_INST_FOREACH_STATUS_OKAY(TEMPERATURE_SENSOR_TX_CONTEXT_COMMA)};

static void jerrycan_temperature_sensor_tx_handler(struct k_work *work);
static K_WORK_DEFINE(jerrycan_temperature_sensor_tx_work, jerrycan_temperature_sensor_tx_handler);

static void jerrycan_temperature_sensor_tx_handler(struct k_work *work) {
    /* For each temperature sensor, construct and send a temperature message*/
    for (int i = 0; i < TEMPERATURE_SENSOR_COUNT; i++) {
        const jerrycan_temperature_sensor_tx_context_t *tx_context = &tx_contexts[i];
        const struct device *temperature_sensor = tx_context->temperature_sensor;
        struct sensor_value temperature, humidity;
        int ret;

        /* Fetch temperature and humidity */
        ret = sensor_sample_fetch(temperature_sensor);
        if (ret != 0) {
            LOG_ERR("Failed to send CAN message for temperature_sensor%d: Error fetching samples - %s",
                    tx_context->instance_number, strerror(ret));
        }

        /* Get temperature value */
        ret = sensor_channel_get(temperature_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
        if (ret != 0) {
            LOG_ERR("Failed to send CAN message for temperature_sensor%d: Error getting temperature value - %s",
                    tx_context->instance_number, strerror(ret));
        }

        /* Get humidity value */
        ret = sensor_channel_get(temperature_sensor, SENSOR_CHAN_HUMIDITY, &humidity);
        if (ret != 0) {
            LOG_ERR("Failed to send CAN message for temperature_sensor%d: Error getting humidity value - %s",
                    tx_context->instance_number, strerror(ret));
        }

        /*  Populate message with temperature and humidity values from sensor driver */
        jerrycan_msg_t msg = {
            .type = JERRYCAN_CMD_TEMP_HUM_READ,
            .temp_hum_read =
                {
                    .instance = tx_context->instance_number,
                    .temperature = (uint16_t)(sensor_value_to_float(&temperature) * TEMPHUM_SCALE_FACTOR),
                    .humidity = (uint16_t)(sensor_value_to_float(&humidity) * TEMPHUM_SCALE_FACTOR),
                },
        };

        /* Transmit message */
        ret = jerrycan_tx(&msg, K_NO_WAIT);
        if (ret != 0) {
            LOG_ERR("Error sending CAN message for temperature_sensor%d: %d", tx_context->instance_number, ret);
        }
    }
}

static void jerrycan_temperature_sensor_tx(struct k_timer *timer) {
    /* Submit tx_job to workqueue (I2C transaction cannot occur within ISR) */
    int ret = k_work_submit(&jerrycan_temperature_sensor_tx_work);
    switch (ret) {
        case 0: /* fall-through */
            /* work was already submitted to a queue */
        case 1: /* fall-through */
            /* work was not submitted and has been queued to queue */
        case 2:
            /* work was running and has been queued to the queue that was running it */
            break;
        case -EBUSY:  /* fall-through */
        case -EINVAL: /* fall-through */
        case -ENODEV:
            LOG_ERR("Failed to submit jerrycan_temperature_sensor_tx_work to the system workqueue: %s",
                    k_work_submit_error_to_str(-ret));
            break;
        default:
            LOG_ERR("Failed to submit jerrycan_temperature_sensor_tx_work to the system workqueue: Unknown error - %d",
                    ret);
            break;
    }
}

K_TIMER_DEFINE(jerrycan_temperature_sensor_timer, jerrycan_temperature_sensor_tx, NULL);

static int jerrycan_temperature_sensor_init() {
    // Start the timer that will send the status message periodically
    k_timer_start(&jerrycan_temperature_sensor_timer, K_MSEC(100),
                  K_MSEC(CONFIG_LIB_JERRYCAN_TEMPERATURE_TX_PERIOD_MS));
    return 0;
}

SYS_INIT(jerrycan_temperature_sensor_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
