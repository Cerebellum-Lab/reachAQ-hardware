#include "adi_tmc2209.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>

#include "adi_tmc2209_types.h"

#define DT_DRV_COMPAT adi_tmc2209

LOG_MODULE_REGISTER(adi_tmc2209, CONFIG_ADI_TMC2209_DEBUG_LEVEL);

typedef struct adi_tmc2209_config {
    const struct device *uart_dev;
    uint8_t address;  // 0, 1, 2, or 3
} adi_tmc2209_config_t;

typedef struct adi_tmc2209_data {
} adi_tmc2209_data_t;

#define SYNC_NIBBLE 0x05U
#define UART_READ_TRIES 10000U
#define SEND_DELAY 2  // ((N div 2) * 2 + 1) * 8 clock cycles
#define HOST_ADDR 0xFFU

#define SLEEP_DELAY() k_sleep(K_MSEC(10))

/**
 * @returns crc of `data[0 : size]` (Polynomial is x^8 + x^2 + x + 1.)
 */
static uint8_t calculate_crc(const uint8_t *const data, const size_t size) {
    uint8_t crc = 0;

    for (size_t i = 0; i < size; i++) {
        uint8_t byte = data[i];

        for (size_t j = 0; j < CHAR_BIT * sizeof(uint8_t); j++) {
            if ((crc >> 7) ^ (byte & 0x01)) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
            byte >>= 1;
        }
    }

    return crc;
}

/**
 * Write the crc of `data[0 : size - 1]` to `data[size - 1]`.
 *
 * @param data must have at least `size` bytes.
 * @param size must be at least 2.
 *
 * @retval 0 on success
 * @retval -EINVAL if `size` is less than 2.
 */
static int store_crc(uint8_t *data, const size_t size) {
    if (size < 2) {
        return -EINVAL;
    }

    data[size - 1] = calculate_crc(data, size - 1);
    return 0;
}

/**
 * Check that the crc of `data[0 : size - 1]` matches `data[size - 1]`.
 *
 * @retval `true` on success.
 * @retval `false` on failure.
 */
static bool check_crc(const uint8_t *data, const size_t size) {
    if (size < 2) {
        return false;
    }

    return calculate_crc(data, size - 1) == data[size - 1];
}

#define NO_BYTE_WAITING (-1)
static int read_single_line_uart(const struct device *dev, uint8_t *buf, const size_t size) {
    const adi_tmc2209_config_t *config = dev->config;

    for (size_t i = 0; i < size; i++) {
        uint8_t byte;
        int ret;
        int j = 0;
        do {
            if (j > UART_READ_TRIES) {
                LOG_DBG("Timed out reading byte %d", i);
                return -ETIMEDOUT;
            }
            ret = uart_poll_in(config->uart_dev, &byte);
            j++;
        } while (ret == NO_BYTE_WAITING);

        if (ret != 0) {
            LOG_DBG("Failed to read byte %d: %d", i, ret);
            return ret;
        }

        buf[i] = byte;
    }

    return 0;
}

static int write_single_line_uart_and_flush_read(const struct device *dev, const uint8_t *buf, const size_t size) {
    /* Write the datagram to the UART. The driver will echo the datagram back to us so we go ahead
     * and read it to flush it from the buffer.
     */
    const adi_tmc2209_config_t *config = dev->config;
    uint8_t rx_buf[WR_PACKET_LENGTH];
    if (size > sizeof(rx_buf)) {
        LOG_ERR("Datagram too large (%d) for rx buffer (%d).", size, sizeof(rx_buf));
        return -EINVAL;
    }

    for (size_t i = 0; i < size; i++) {
        uart_poll_out(config->uart_dev, buf[i]);
    }

    return read_single_line_uart(dev, rx_buf, size);
}

/**
 * Write `data` to `reg_address` on this device. Data is 4 bytes long!
 *
 * @retval 0 on success,
 * @retval -errno on error.
 */
static int adi_tmc2209_write(const struct device *dev, const uint8_t reg_address, adi_tmc2209_reg_t data) {
    const adi_tmc2209_config_t *config = dev->config;

    // Convert from host to network byte order
    data.as_uint32 = sys_cpu_to_be32(data.as_uint32);

    write_datagram_t datagram = {{
        .sync = SYNC_NIBBLE,
        .reserved = 0,
        .address = config->address,
        .reg_address = reg_address,
        .rw = WRITE,
        .data = data,
        .crc = 0,  // appease warnings
    }};
    SLEEP_DELAY();
    store_crc(datagram.raw, sizeof(datagram.raw));
    write_single_line_uart_and_flush_read(dev, datagram.raw, sizeof(datagram.raw));

    return 0;
}

/**
 * Read `data` from `reg_address` on `device`.
 *
 * @param dev Device handle; must not be NULL
 * @param reg_address
 * @param data must be 4 bytes long.
 *
 * @retval 0 on success
 * @retval -errno on error.
 */
static int adi_tmc2209_read(const struct device *dev, const uint8_t reg_address, adi_tmc2209_reg_t *data) {
    const adi_tmc2209_config_t *config = dev->config;

    if (data == NULL) {
        return -EINVAL;
    }

    read_datagram_t datagram = {{
        .sync = SYNC_NIBBLE,
        .reserved = 0x0,
        .address = config->address,
        .reg_address = reg_address,
        .rw = READ,
        .crc = 0,  // appease warnings
    }};

    store_crc(datagram.raw, sizeof(datagram.raw));
    SLEEP_DELAY();
    write_single_line_uart_and_flush_read(dev, datagram.raw, sizeof(datagram.raw));

    read_reply_datagram_t reply_datagram = {0};

    const int ret = read_single_line_uart(dev, reply_datagram.raw, sizeof(reply_datagram.raw));

    if (ret != 0) {
        LOG_ERR("Failed (%d) to read reply datagram for %d. Bytes: %02X %02X %02X %02X %02X %02X %02X %02X", ret,
                config->address, reply_datagram.raw[0], reply_datagram.raw[1], reply_datagram.raw[2],
                reply_datagram.raw[3], reply_datagram.raw[4], reply_datagram.raw[5], reply_datagram.raw[6],
                reply_datagram.raw[7]);
        return ret;
    }

    if (!check_crc(reply_datagram.raw, sizeof(reply_datagram.raw))) {
        LOG_ERR("CRC check failed on reply datagram");
        return -EIO;
    }

    if (reply_datagram.fields.sync != SYNC_NIBBLE || reply_datagram.fields.address != HOST_ADDR ||
        reply_datagram.fields.reg_address != reg_address) {
        LOG_ERR("Unexpected data in reply datagram");
        return -EIO;
    }

    // Convert the endianess of the response data
    data->as_uint32 = sys_be32_to_cpu(reply_datagram.fields.data.as_uint32);

    return 0;
}

/**
 * Configure GCONF to use:
 * * internal voltage reference,
 * * external sense resistors,
 * * StealthChop,
 * * UART on the PDN_UART pin,
 * * microstep resolution using mres rather than MS1 and MS2, and
 * * no multistep filtering.
 *
 * Use the existing configurations for shaft direction, index_otpw, index_stop.
 *
 * Fails if the test_mode bit is set.
 *
 * @param dev Device to configure
 * @return 0 on success, -errno on error
 */
static int adi_tmc2209_set_default_gconf_stealthchop(const struct device *dev) {
    const adi_tmc2209_config_t *config = dev->config;
    adi_tmc2209_reg_t reg;
    int ret = adi_tmc2209_read(dev, REG_GCONF, &reg);
    if (ret != 0) {
        LOG_ERR("[Dev: %d], couldn't read GCONF! %d", config->address, ret);
        // There may be garbage in `reg`
        reg.as_uint32 = 0;
    }

    if (reg.gconf.test_mode_DO_NOT_USE) {
        LOG_ERR("[Dev: %d], GCONF test mode enabled, not supporteed!", config->address);
        return -ENOTSUP;
    }

    reg.gconf.i_scale_analog = 0;
    reg.gconf.internal_Rsense = 0;
    reg.gconf.en_spreadcycle = 0;
    reg.gconf.pdn_disable = 1;
    reg.gconf.mstep_reg_select = 1;
    reg.gconf.multistep_filt = 0;
    ret = adi_tmc2209_write(dev, REG_GCONF, reg);
    return ret;
}

int adi_tmc2209_set_ihold_irun(const struct device *dev, const uint8_t hold_current, const uint8_t run_current,
                               const uint8_t hold_delay) {
    if (hold_current > 32 || hold_current < 1 || run_current > 32 || run_current < 1 || hold_delay > 15) {
        LOG_ERR("Invalid arguments for setting IHOLD_IRUN register.");
        return -EINVAL;
    }

    adi_tmc2209_reg_t val = {0};

    val.ihold_irun.ihold = hold_current - 1;
    val.ihold_irun.irun = run_current - 1;
    val.ihold_irun.iholddelay = hold_delay;
    return adi_tmc2209_write(dev, REG_IHOLD_IRUN, val);
}

int adi_tmc2209_set_microstep(const struct device *dev, const uint32_t steps_per_fullstep) {
    const adi_tmc2209_config_t *config = dev->config;
    adi_tmc2209_reg_t val = {0};
    int ret = adi_tmc2209_read(dev, REG_CHOPCONF, &val);

    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to read chopconf data", config->address, ret);
        return -EIO;
    }

    uint8_t mres = 0;

    switch (steps_per_fullstep) {
        case 1:
            mres = 8;
            break;
        case 2:
            mres = 7;
            break;
        case 4:
            mres = 6;
            break;
        case 8:
            mres = 5;
            break;
        case 16:
            mres = 4;
            break;
        case 32:
            mres = 3;
            break;
        case 64:
            mres = 2;
            break;
        case 128:
            mres = 1;
            break;
        case 256:
            mres = 0;
            break;
        default:
            LOG_ERR("[Dev: %d] Invalid microstep: %u", config->address, steps_per_fullstep);
            return -EINVAL;
    }

    val.chopconf.mres = mres;
    ret = adi_tmc2209_write(dev, REG_CHOPCONF, val);
    return ret;
}

/**
 * Set CHOPCONF with our settings for StealthChop mode, to wit:
 * * Do not use double-edged step impulses (`dedge`). It is incompatible with the
 *   mode of the STM32 timer peripheral we are using.
 * * Use full (256-microstep) interpolation.
 * * Use full vsense.
 * * Set tbl = 0.
 * * Set toff to default (if it is 0, the driver does not operate).
 * * Set hend and hstrt to default values for StealthChop
 *
 * @param dev
 * @return
 */
static int adi_tmc2209_set_chopconf_stealthchop(const struct device *dev) {
    const adi_tmc2209_config_t *config = dev->config;
    adi_tmc2209_reg_t val = {0};
    int ret = adi_tmc2209_read(dev, REG_CHOPCONF, &val);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to read chopconf data", config->address, ret);
        val.as_uint32 = 0x10000053;  // reset default
    }

    val.chopconf.dedge = 0;
    val.chopconf.intpol = 1;
    val.chopconf.vsense = 0;
    val.chopconf.tbl = 0;
    val.chopconf.hend = 0;
    val.chopconf.hstrt = 5;
    val.chopconf.toff = 3;
    ret = adi_tmc2209_write(dev, REG_CHOPCONF, val);
    return ret;
}

/**
 * Set the PWMCONF register for our application.
 * * Set it to freewheel if we haven't already.
 * * Set pwm_freq to 1 per datasheet suggestion for internal clock (p. 40).
 * * Set autograd to no and fill in PWM_GRAD and PWM_OFS with known good values.
 *
 * @param dev device to configure
 * @return 0 on success, -errno on error
 */
static int adi_tmc2209_set_pwmconf_stealthchop(const struct device *dev) {
    const adi_tmc2209_config_t *config = dev->config;
    adi_tmc2209_reg_t val = {0};

    int ret = adi_tmc2209_read(dev, REG_PWMCONF, &val);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to read pwmconf", config->address, ret);
        val.as_uint32 = 0xC10D0024;  // Reset default
    }

    val.pwmconf.pwm_autograd = 0;
    val.pwmconf.pwm_autoscale = 1;
    val.pwmconf.freewheel = 1;
    val.pwmconf.pwm_freq = 1;
    val.pwmconf.pwm_grad = 0x76;
    val.pwmconf.pwm_ofs = 0xff;

    ret = adi_tmc2209_write(dev, REG_PWMCONF, val);
    return ret;
}

/**
 * Disable coolstep on `dev`. It is unreliable on these motors.
 *
 * @return 0 on success, -errno on failure
 */
static int adi_tmc2209_coolstep_disable(const struct device *dev) {
    const adi_tmc2209_config_t *config = dev->config;
    adi_tmc2209_reg_t reg = {0};
    int ret = adi_tmc2209_read(dev, REG_COOLCONF, &reg);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to read coolstep", config->address, ret);
        reg.as_uint32 = 0;
    }
    reg.coolconf.semin = 0;
    ret = adi_tmc2209_write(dev, REG_COOLCONF, reg);
    return ret;
}

/**
 * Configure this driver according to our motors' characteristics. Some general notes:
 * * We are using the internal clock which is factory trimmed to 12MHz.
 * * We want to *always* use StealthChop. SpreadCycle is very bad on these motors.
 *
 * @param dev
 * @return 0 on success, -errno on error
 */
static int adi_tmc2209_init(const struct device *dev) {
    const adi_tmc2209_config_t *config = dev->config;

    adi_tmc2209_reg_t val = {0};

    val.as_uint32 = 0;
    // Clear reset and print diagnostics from GSTAT.
    int ret = adi_tmc2209_read(dev, REG_GSTAT, &val);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to read gstat data", config->address, ret);
    } else {
        LOG_DBG("[Dev: %d] GSTAT flags reset [%d] drv_err [%d] uv_cp [%d]", config->address, val.gstat.reset,
                val.gstat.drv_err, val.gstat.uv_cp);

        // Write back to clear the status flags.
        ret = adi_tmc2209_write(dev, REG_GSTAT, val);
        if (ret < 0) {
            LOG_ERR("Failed (%d) to write gstat data to reset", ret);
        }
    }

    // Configure SEND_DELAY to avoid issues with other drivers.
    val.nodeconf.send_delay = SEND_DELAY;
    adi_tmc2209_write(dev, REG_NODECONF, val);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to write nodeconf", config->address, ret);
    }

    // Configure GCONF next to avoid PDN_UART pin weirdness.
    ret = adi_tmc2209_set_default_gconf_stealthchop(dev);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to set default GCONF", config->address, ret);
    }

    // Set IHOLD and IRUN according to our motor's characteristics.
    const uint8_t default_irun = 9;         // 9 => 353mA RMS current (nominal for these motors)
    const uint8_t default_ihold = 1;        // Use as little current as possible in standstill to reduce heating.
    const uint8_t default_iholddelay = 15;  // Use the greatest time because otherwise it's choppy/
    ret = adi_tmc2209_set_ihold_irun(dev, default_ihold, default_irun, default_iholddelay);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to set irun", config->address, ret);
    }

    // Set TPWMTHRS to 0 so that StealthChop is always enabled
    val.as_uint32 = 0;
    ret = adi_tmc2209_write(dev, REG_TPWMTHRS, val);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to write TPWMTHRS -- SpreadCycle may become enabled", config->address, ret);
    }

    // Set single-stepping by default
    ret = adi_tmc2209_set_microstep(dev, 1);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to set microstep", config->address, ret);
    }

    ret = adi_tmc2209_set_chopconf_stealthchop(dev);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to set chopconf", config->address, ret);
    }

    ret = adi_tmc2209_set_pwmconf_stealthchop(dev);
    if (ret < 0) {
        LOG_ERR("[Dev: %d] Failed (%d) to set pwmconf", config->address, ret);
    }

    return ret;
}

static struct adi_tmc2209_driver_api adi_tmc2209_driver_api = {
    .write = adi_tmc2209_write,
    .read = adi_tmc2209_read,
};

#define ADI_TMC2209_INIT(n)                                                                                       \
    static const adi_tmc2209_config_t adi_tmc2209_config_##n = {                                                  \
        .uart_dev = DEVICE_DT_GET(DT_INST_BUS(n)),                                                                \
        .address = DT_INST_REG_ADDR(n),                                                                           \
    };                                                                                                            \
    static adi_tmc2209_data_t adi_tmc2209_data_##n;                                                               \
    DEVICE_DT_INST_DEFINE(n, adi_tmc2209_init, NULL, &adi_tmc2209_data_##n, &adi_tmc2209_config_##n, POST_KERNEL, \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &adi_tmc2209_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ADI_TMC2209_INIT)
