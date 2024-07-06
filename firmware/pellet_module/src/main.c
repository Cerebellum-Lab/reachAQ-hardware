#include <app_version.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "generic_gpios.h"
#include "placeholder.h"
#include "servo.h"

LOG_MODULE_REGISTER(app);

const struct device *gpio_dev = DEVICE_DT_GET_ANY(ll_generic_gpios);
const struct device *servo_dev = DEVICE_DT_GET_ANY(ll_servo);

static void on_input_func(struct input_event *evt) {
    LOG_INF("Input event: type=%d, code=%d, value=%d", evt->type, evt->code, evt->value);
}

INPUT_CALLBACK_DEFINE(NULL, on_input_func);

static uint32_t triangle_wave[101];
static uint32_t zero_wave[10] = {0};

int main() {
    LOG_INF("Autotrainer Pellet Module v%s", APP_VERSION_STRING);

    // Read the DeviceType bits to make sure this is a Pellet Module
    uint32_t device_type;
    ll_generic_gpio_read(gpio_dev, 0x3, &device_type);

    // Read the NodeID to program up the CAN_ID properly
    uint32_t node_id;
    ll_generic_gpio_read(gpio_dev, 0xC, &node_id);
    node_id = (node_id >> 2) & 0x3;

    // Print out the device type and node ID
    LOG_INF("CAN_ID: 0x%X", (node_id << 2) | device_type);

    placeholder_function();

    // Create a triangle wave from 5% to 10% duty cycle
    for (int i = 0; i < ARRAY_SIZE(triangle_wave); i++) {
        triangle_wave[i] = (i * (2000 / (ARRAY_SIZE(triangle_wave) - 1))) + 2000;
    }
    sys_cache_data_flush_range(triangle_wave, sizeof(triangle_wave));


    while (true) {
        LOG_INF("Queueing servo positions");
        int ret = ll_queue_servo_positions(servo_dev, triangle_wave, ARRAY_SIZE(triangle_wave), K_FOREVER);
        ret |= ll_queue_servo_positions(servo_dev, zero_wave, ARRAY_SIZE(zero_wave), K_FOREVER);
        if (ret < 0) {
            LOG_ERR("Failed to start servo DMA: %d", ret);
            goto error;
        }
    }

error:
    while (true) {
        k_msleep(500);
    }
}
