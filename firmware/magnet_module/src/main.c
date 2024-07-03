#include <app_version.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include "placeholder.h"


LOG_MODULE_REGISTER(app);

int main() {
    LOG_INF("Autotrainer Magnet Module v%s", APP_VERSION_STRING);

    placeholder_function();

    while (true) {
        k_msleep(100);
    }

error:
    while (true) {
        k_msleep(500);
    }
}
