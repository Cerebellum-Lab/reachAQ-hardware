#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, LOG_LEVEL_DBG);

static void jerrycan_temperature_sensor_tx() {}

K_TIMER_DEFINE(jerrycan_temperature_sensor_timer, jerrycan_temperature_sensor_tx, NULL);

static int jerrycan_temperature_sensor_init() {
    // Start the timer that will send the status message periodically
    k_timer_start(&jerrycan_temperature_sensor_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_TEMPERATURE_TX_RATE));
    return 0;
}

SYS_INIT(jerrycan_temperature_sensor_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
