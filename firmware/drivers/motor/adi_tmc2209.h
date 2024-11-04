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

/**
 * Set the IHOLD_IRUN register according to register values.
 *
 * @param dev ADI TMC2209 device to be modified.
 * @param hold_current Hold current as numerator of 32.
 * @param run_current Run current as numerator of 32.
 * @param hold_delay Delay after STEP pin goes low before current is reduced, in 2^18 clocks.
 * @return 0 on success, -EINVAL if the current values are out of range, -errno on IO error.
 */
int adi_tmc2209_set_ihold_irun(const struct device *dev, uint8_t hold_current, uint8_t run_current, uint8_t hold_delay);