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
     * and read it to flush it from the buffer. Occasionally, the second byte (aka, byte 1) will be
     * missing from the re-read. This is very odd! And it happens despite `single-wire` setting or
     * speed setting in the device tree. The code detects this and takes care of either
     * the case where it omits the second byte or the case where we read the full datagram.
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

int adi_tmc2209_set_ihold_irun(const struct device *dev, const uint8_t hold_current, const uint8_t run_current,
                               const uint8_t hold_delay) {
    if (hold_current > 32 || hold_current < 1 || run_current > 32 || run_current < 1 || hold_delay > 15) {
        LOG_ERR("Invalid arguments for setting IHOLD_IRUN register.");
        return -EINVAL;
    }

    adi_tmc2209_reg_t val = {
        .ihold_irun =
            {
                .ihold = hold_current - 1,
                .irun = run_current - 1,
                .iholddelay = hold_delay,
            },
    };

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

    val.chopconf.intpol = 1;
    val.chopconf.dedge = 0;
    val.chopconf.tbl = 2;
    val.chopconf.hstrt = 4;
    val.chopconf.hend = 0;
    val.chopconf.mres = mres;
    k_sleep(K_MSEC(10));
    ret = adi_tmc2209_write(dev, REG_CHOPCONF, val);
    return ret;
}

static int adi_tmc2209_init(const struct device *dev) {
    const int default_hold_current = 1;
    const int default_run_current = 4;
    const int default_hold_delay = 15;

    // Check GSTAT & clear reset
    adi_tmc2209_reg_t val;
    int ret = adi_tmc2209_read(dev, REG_GSTAT, &val);
    if (ret < 0) {
        LOG_ERR("Failed (%d) to read gstat data", ret);
    } else {
        LOG_DBG("GSTAT flags reset [%d] drv_err [%d] uv_cp [%d]", val.gstat.reset, val.gstat.drv_err, val.gstat.uv_cp);

        // Write back to clear the status flags
        ret = adi_tmc2209_write(dev, REG_GSTAT, val);
        if (ret < 0) {
            LOG_ERR("Failed (%d) to write gstat data to reset", ret);
        }
    }

    k_sleep(K_MSEC(10));

    val.as_uint32 = 0;
    val.nodeconf.send_delay = SEND_DELAY;
    adi_tmc2209_write(dev, REG_NODECONF, val);

    ret = adi_tmc2209_read(dev, REG_GCONF, &val);
    struct GCONF_data_fields gconf_data = val.gconf;

    if (ret < 0) {
        LOG_WRN("Couldn't read GCONF. Setting defaults.");
        memset(&gconf_data, 0, sizeof(gconf_data));  // There may be garbage in the struct.
    }

    if (gconf_data.test_mode_DO_NOT_USE) {
        LOG_ERR("Test mode is enabled on the chip. This is not supported.");
        return -ENOTSUP;
    }

    gconf_data.i_scale_analog = 0;   // Use internal voltage reference. VREF is disconnected on the board.
    gconf_data.internal_Rsense = 0;  // External sense resistors are connected.
    gconf_data.en_spreadcycle = 0;   // Use StealthChop
    gconf_data.shaft = 0;            // Do not invert the direction of the motor.
    // Don't set or unset index_otpw or index_step. The INDEX pin is disconnected.
    gconf_data.pdn_disable = 1;       // Using UART so set this per datasheet.
    gconf_data.mstep_reg_select = 1;  // MS1 and MS2 are not connected so use UART registers.
    gconf_data.multistep_filt = 0;

    k_sleep(K_MSEC(10));  // I don't know why this works. Are we writing too soon after reading? but it is necessary.
    val.gconf = gconf_data;
    ret = adi_tmc2209_write(dev, REG_GCONF, val);

    if (ret < 0) {
        LOG_ERR("Couldn't write GCONF. Defaults may be wrong!");
    }

    ret = adi_tmc2209_read(dev, REG_GCONF, &val);
    gconf_data = val.gconf;
    if (ret < 0) {
        LOG_ERR("Couldn't read GCONF. Defaults may be wrong!");
    } else {
        LOG_DBG(
            "GCONF data: pdn_disable = %d, internal_Rsense = %d, en_spreadcycle = %d, shaft = %d, i_scale_analog = %d",
            gconf_data.pdn_disable, gconf_data.internal_Rsense, gconf_data.en_spreadcycle, gconf_data.shaft,
            gconf_data.i_scale_analog);
    }

    k_sleep(K_MSEC(10));  // added here out of an abundance of caution, see comment above.
    ret = adi_tmc2209_set_ihold_irun(dev, default_hold_current, default_run_current, default_hold_delay);
    if (ret < 0) {
        LOG_ERR("Failed (%d) to set IHOLD_IRUN register", ret);
    }

    // FULLSTEP by default.
    k_sleep(K_MSEC(10));
    ret = adi_tmc2209_set_microstep(dev, 1);
    if (ret < 0) {
        LOG_ERR("Failed (%d) to set microstep", ret);
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
