#include "adi_tmc2209.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#define DT_DRV_COMPAT adi_tmc2209

LOG_MODULE_REGISTER(adi_tmc2209, LOG_LEVEL_DBG);

typedef struct adi_tmc2209_config {
    const struct device *uart_dev;
    uint8_t address;  // 0, 1, 2, or 3
} adi_tmc2209_config_t;

typedef struct adi_tmc2209_data {
} adi_tmc2209_data_t;

#define SYNC_NIBBLE 0x5U
#define UART_READ_TRIES 1000000U
#define HOST_ADDR 0xFFU

typedef enum rw_bit {
    READ = 0,
    WRITE = 1,
} rw_bit_t;

struct __attribute__((packed)) write_datagram_fields {
    uint8_t sync : 4;      // invariably 0b0101 = 0x5
    uint8_t reserved : 4;  // Doesn't matter but contributes to CRC

    uint8_t address;  // 0, 1, 2, or 3

    uint8_t reg_address : 7;
    rw_bit_t rw : 1;  // 1 for write

    uint8_t data[4];  // Data to write

    uint8_t crc;
};

typedef union write_datagram {
    struct write_datagram_fields fields;
    uint8_t raw[8];
} write_datagram_t;

BUILD_ASSERT(sizeof(struct write_datagram_fields) == sizeof(write_datagram_t));
BUILD_ASSERT(sizeof(write_datagram_t) == 8U);

struct __attribute__((packed)) read_datagram_fields {
    uint8_t sync : 4;      // invariably 0x5
    uint8_t reserved : 4;  // Doesn't matter but contributes to CRC

    uint8_t address;  // 0, 1, 2, or 3

    uint8_t reg_address : 7;
    rw_bit_t rw : 1;  // 0 for read

    uint8_t crc;
};

typedef union read_datagram {
    struct read_datagram_fields fields;
    uint8_t raw[4];
} read_datagram_t;

BUILD_ASSERT(sizeof(struct read_datagram_fields) == sizeof(read_datagram_t));
BUILD_ASSERT(sizeof(read_datagram_t) == 4U);

struct __attribute__((packed)) read_reply_datagram_fields {
    uint8_t sync : 4;      // invariably 0b0101
    uint8_t reserved : 4;  // Doesn't matter but contributes to CRC

    uint8_t address;  // master address: always 0b11111111

    uint8_t reg_address : 7;
    rw_bit_t rw : 1;  // 0 for read

    uint8_t data[4];  // Data read

    uint8_t crc;
};

typedef union read_reply_datagram {
    struct read_reply_datagram_fields fields;
    uint8_t raw[8];
} read_reply_datagram_t;

BUILD_ASSERT(sizeof(struct read_reply_datagram_fields) == sizeof(read_reply_datagram_t));
BUILD_ASSERT(sizeof(read_reply_datagram_t) == 8U);

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
    uint8_t rx_buf[8];
    if (size > sizeof(rx_buf)) {
        LOG_ERR("Datagram too large for rx buffer.");
        return -EINVAL;
    }

    for (size_t i = 0; i < size; i++) {
        uart_poll_out(config->uart_dev, buf[i]);
    }

    // Wait for the datagram we just sent to be "received" by the driver.
    size_t bytes_read = 0;
    while (bytes_read < size) {
        const int ret = read_single_line_uart(dev, &rx_buf[bytes_read], 1);
        if (ret == -ETIMEDOUT) {
            // Write was in all likelihood successful and no more bytes are waiting.
            LOG_DBG("Timed out while trying to re-read written data.");
            return 0;
        }
        if (rx_buf[bytes_read] != buf[bytes_read]) {
            // Assume we missed the previous byte.
            rx_buf[bytes_read + 1] = rx_buf[bytes_read];
            rx_buf[bytes_read] = buf[bytes_read];
            if (bytes_read == 1 || (bytes_read == 2 && buf[1] == buf[2])) {
                // This is the "normal" case of missing the byte 1. Note the second conditional which is necessary
                // if buf[1] == buf[2]. Do ~nothing.
                LOG_DBG("Missed byte 1 over single line uart (somewhat frequent/expected).");
            } else {
                LOG_ERR("Missed byte %d over single line uart.", bytes_read);
            }
            bytes_read++;
        }
        bytes_read++;
    }

    return 0;
}

/**
 * Write `data` to `reg_address` on this device. Data is 4 bytes long!
 *
 * @retval 0 on success,
 * @retval -errno on error.
 */
static int adi_tmc2209_write(const struct device *dev, const uint8_t reg_address, const uint8_t *data) {
    const adi_tmc2209_config_t *config = dev->config;

    write_datagram_t datagram = {{
        .sync = SYNC_NIBBLE,
        .reserved = 0,
        .address = config->address,
        .reg_address = reg_address,
        .rw = WRITE,
        .data = {data[0], data[1], data[2], data[3]},
        .crc = 0,  // appease warnings
    }};

    store_crc(datagram.raw, sizeof(datagram.raw));

    write_single_line_uart_and_flush_read(dev, datagram.raw, sizeof(datagram.raw));

    return 0;
}

/**
 * Read `data` from `reg_address` on `device`.
 *
 * @param data must be at least 4 bytes long.
 *
 * @retval 0 on success
 * @retval -errno on error.
 */
static int adi_tmc2209_read(const struct device *dev, const uint8_t reg_address, uint8_t *data) {
    const adi_tmc2209_config_t *config = dev->config;

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
        LOG_ERR("Failed to read reply datagram: %d. Bytes: %02X %02X %02X %02X %02X %02X %02X %02X", ret,
                reply_datagram.raw[0], reply_datagram.raw[1], reply_datagram.raw[2], reply_datagram.raw[3],
                reply_datagram.raw[4], reply_datagram.raw[5], reply_datagram.raw[6], reply_datagram.raw[7]);
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

    // The size of `data` is inevitably 4, as all registers on the TMC2209 are 32 bits long.
    for (size_t i = 0; i < sizeof(reply_datagram.fields.data); i++) {
        data[i] = reply_datagram.fields.data[i];
    }

    return 0;
}

static int adi_tmc2209_init(const struct device *dev) { return 0; }

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
