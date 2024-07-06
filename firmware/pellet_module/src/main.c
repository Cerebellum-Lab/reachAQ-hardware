#include <app_version.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "generic_gpios.h"
#include "placeholder.h"

LOG_MODULE_REGISTER(app);

const struct device *gpio_dev = DEVICE_DT_GET_ANY(ll_generic_gpios);

static void on_input_func(struct input_event *evt) {
    LOG_INF("Input event: type=%d, code=%d, value=%d", evt->type, evt->code, evt->value);
}

INPUT_CALLBACK_DEFINE(NULL, on_input_func);

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

    while (true) {
        k_msleep(100);
    }

error:
    while (true) {
        k_msleep(500);
    }
}
