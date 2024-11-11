/**
 * @file heartbeat.c
 * @brief JerryCAN Heartbeat Message Handling
 *
 * This file provides functionality to manage incoming heartbeat messages recieved via
 * the JerryCAN library. Whenever a CAN heartbeat message is received, the heartbeat LED
 * blinks briefly, providing a visual indication that the system is alive and responsive to messages.
 *
 * Timer Rate: CONFIG_LIB_JERRYCAN_HEARTBEAT_LED_DURATION_MS ms (configured in Kconfig)
 * - Controls the duration for which the LED stays on when a heartbeat message is received.
 *
 * Key Functions:
 * - `heartbeat_led_start()`: Turns on the heartbeat LED and starts a timer
 *    to turn it off automatically.
 * - `heartbeat_handler()`: Handles incoming heartbeat messages and initiates the LED blink.
 * - `jerrycan_heartbeat_init()`: Registers the heartbeat message handler for CAN messages.
 *
 * Dependencies:
 * - `gpio_pin_set_dt()`: Sets the state of the GPIO pin connected to the heartbeat LED.
 * - `jerrycan_register_rx_callback()`: Registers a CAN message callback in the JerryCAN system.
 *
 * Usage:
 * This module is initialized at startup using Zephyr’s SYS_INIT macro. It registers
 * a callback to blink the LED on receipt of a heartbeat message. The LED will automatically
 * turn off after CONFIG_LIB_JERRYCAN_HEARTBEAT_LED_DURATION_MS ms, using a timer to control the blink duration.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

/*
 * Heartbeat LED
 */
static const struct gpio_dt_spec heartbeat_led_dev = GPIO_DT_SPEC_GET(DT_NODELABEL(status_led), gpios);

static void heartbeat_led_off(struct k_timer *timer) { gpio_pin_set_dt(&heartbeat_led_dev, 0); }

K_TIMER_DEFINE(heartbeat_led_timer, heartbeat_led_off, NULL);

static void heartbeat_led_start() {
    // Turn on the LED and then start a timer that will turn
    // it off in CONFIG_LIB_JERRYCAN_HEARTBEAT_LED_DURATION_MS ms
    gpio_pin_configure_dt(&heartbeat_led_dev, GPIO_OUTPUT);
    gpio_pin_set_dt(&heartbeat_led_dev, 1);
    k_timer_start(&heartbeat_led_timer, K_MSEC(CONFIG_LIB_JERRYCAN_HEARTBEAT_LED_DURATION_MS), K_NO_WAIT);
}

static void heartbeat_handler(jerrycan_msg_t *msg) {
    // If we receive a heartbeat message, we should blink the LED
    heartbeat_led_start();
}

static jerrycan_rx_callback_t heartbeat_callback = {
    .filter_msg_type = JERRYCAN_CMD_HEARTBEAT,
    .func = heartbeat_handler,
};

static int jerrycan_heartbeat_init() {
    // Register an handler for HEARTBEAT messages
    return jerrycan_register_rx_callback(&heartbeat_callback);
}

SYS_INIT(jerrycan_heartbeat_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
