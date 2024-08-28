#include <app_version.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app);

int main() {
    LOG_INF("Autotrainer Magnet Module v%s", APP_VERSION_STRING);

    while (true) {
        k_msleep(1000);
    }

error:
    while (true) {
        k_msleep(500);
    }
}
