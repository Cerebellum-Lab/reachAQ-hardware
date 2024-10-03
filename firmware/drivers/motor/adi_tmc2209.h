#include <zephyr/device.h>

/*
 * This low-level API allows for bare register reads and writes to the TMC2209.
 * It should probably only be used by the driver itself, but is exposed here for
 * the shell to use.
 */
struct adi_tmc2209_driver_api {
    int (*write)(const struct device *dev, uint8_t reg_address, const uint8_t *data);
    int (*read)(const struct device *dev, uint8_t reg_address, uint8_t *data);
};
