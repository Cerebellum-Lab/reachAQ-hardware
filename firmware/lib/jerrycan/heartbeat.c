#include <zephyr/drivers/gpio.h>

#include "jerrycan.h"

/*
 * Heartbeat LED
 */
static const struct gpio_dt_spec heartbeat_led_dev = GPIO_DT_SPEC_GET(DT_NODELABEL(status_led), gpios);

static void heartbeat_led_off(struct k_timer *timer) { gpio_pin_set_dt(&heartbeat_led_dev, 0); }

K_TIMER_DEFINE(heartbeat_led_timer, heartbeat_led_off, NULL);

static void heartbeat_led_start() {
    // Turn on the LED and then start a timer that will turn it off in 100ms
    gpio_pin_configure_dt(&heartbeat_led_dev, GPIO_OUTPUT);
    gpio_pin_set_dt(&heartbeat_led_dev, 1);
    k_timer_start(&heartbeat_led_timer, K_MSEC(100), K_NO_WAIT);
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
