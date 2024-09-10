#include <app_version.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jerrycan.h"

LOG_MODULE_REGISTER(app);

int main() {
    LOG_INF("Autotrainer Pellet Module v%s", APP_VERSION_STRING);

    while (true) {
        // This runs the main CAN RX loop
        // Callbacks registered for specific messages types will be called when those messages are received
        jerrycan_run(K_MSEC(100));
    }
}
