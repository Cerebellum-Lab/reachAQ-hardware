/**
 * @file led.c
 * @brief JerryCAN Analog Out Message Handling
 *
 * This file provides functionality to manage and transmit analog output information
 * via CAN messages through the JerryCAN library. Incomming commands on the CANbus
 * will adjust the associated LED.
 *
 * The LED is an RGB LED, with a separate PWM line controlling each color.
 * The LED set is only on the Pellet Module.
 *
 * Key Functions:
 * - `jerrycan_led_write_handler()`: Processes CAN messages to update LED RGB values
 * - `jerrycan_led_init()`: Initializes the LED module (timers) and registers
 *    the CAN message callback.
 *
 * Usage:
 * This module is initialized automatically at startup using Zephyr’s SYS_INIT macro,
 * and it will start periodically transmitting analog output data over CAN.
 */

#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define RGB_LED_NODE_ID DT_NODELABEL(rgb_led)
#define RGB_LED DEVICE_DT_GET(RGB_LED_NODE_ID)

#if DT_NODE_EXISTS(RGB_LED_NODE_ID)

static const char* gLabels[] = {DT_FOREACH_CHILD_SEP_VARGS(RGB_LED_NODE_ID, DT_PROP_OR, (, ), label, NULL)};

static const int LED_COUNT = ARRAY_SIZE(gLabels);

/* Used to store last values written to RGB LED */
static uint8_t red, green, blue = 0;

static void jerrycan_rgb_led_tx();

/**
 * Handle incoming JERRYCAN_CMD_RGB_LED messages. Update the RBG levels of
 * of led triplet.
 *
 * @param msg - Guaranteed to be of type JERRYCAN_CMD_RGB_LED
 */
static int jerrycan_rgb_led_msg_handler(const jerrycan_msg_t* msg) {
    const struct device* device = RGB_LED;

    /* Update state for use by status message */
    red = MIN(msg->rgb_led.red, 100);
    green = MIN(msg->rgb_led.green, 100);
    blue = MIN(msg->rgb_led.blue, 100);

    led_set_brightness(device, 0, red);
    led_set_brightness(device, 1, green);
    led_set_brightness(device, 2, blue);

    LOG_INF("Updated RGB LED to (%d, %d, %d)", red, green, blue);

    return 0;
}

/* -------------------------------------------------------------------------- */

// must be static, not on stack
static jerrycan_rx_callback_t led_callback = {
    .filter_msg_type = JERRYCAN_CMD_RGB_LED,
    .func = jerrycan_rgb_led_msg_handler,
    .node = {.next = NULL},
};

static void jerrycan_rgb_led_tx() {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_RGB_LED,
        .rgb_led =
            {
                .red = red,
                .green = green,
                .blue = blue,
            },
    };

    jerrycan_tx(&msg, K_NO_WAIT);
}

K_TIMER_DEFINE(jerrycan_rgb_led_timer, jerrycan_rgb_led_tx, NULL);

/**
 * Initialize the support for commanding the RGB LED.
 *
 * @retval 0 - OK
 * @retval <0 - Error
 */
static int jerrycan_rgb_led_init() {
    const struct device* device = RGB_LED;
    if (!device_is_ready(device)) {
        LOG_ERR("RGB LED is not ready");
        return -EBUSY;
    }

    int ret = 0;
    for (int i = 0; i < LED_COUNT; ++i) {
        const int rc = led_set_brightness(device, i, 0);
        if (rc < 0) {
            ret = rc;
            LOG_ERR("Setting initial brightness for %s RGB LED failed: %d", gLabels[i], rc);
        }
    }

    if (ret == 0) {
        jerrycan_register_rx_callback(&led_callback);
    }

    k_timer_start(&jerrycan_rgb_led_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_RGB_LED_STATUS_PERIOD_MS));

    LOG_INF("RGB LED initialized.");

    return ret;
}

SYS_INIT(jerrycan_rgb_led_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
#else
#warning "RGB LED control disabled."
#endif
