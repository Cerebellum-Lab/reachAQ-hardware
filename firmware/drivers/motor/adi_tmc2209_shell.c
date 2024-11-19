#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>

#include "adi_tmc2209.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *adi_devs[] = {DT_FOREACH_STATUS_OKAY(adi_tmc2209, DEV_GET_COMMA)};

static int cmd_read_register(const struct shell *shell, const int argc, const char *argv[]) {
    if (argc < 3) {
        shell_print(shell, "Usage: %s <device> 0x<register>", argv[0]);
        return -EINVAL;
    }

    char *endptr;
    const int device = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Invalid device number: %s", argv[1]);
        return -EINVAL;
    }

    if (device < 0 || device >= ARRAY_SIZE(adi_devs)) {
        shell_print(shell, "Invalid device number: %d", device);
        return -EINVAL;
    }

    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;

    const int reg = strtol(argv[2], &endptr, 16);
    if (*endptr != '\0') {
        shell_print(shell, "Invalid register: %s", argv[2]);
        return -EINVAL;
    }

    if (reg < 0 || reg > 0xFF) {
        shell_print(shell, "Invalid register: 0x%02X", reg);
        return -EINVAL;
    }

    uint8_t data[4];
    const int ret = api->read(dev, reg, data);

    shell_print(shell, "Data at 0x%02X: 0x%02X 0x%02X 0x%02X 0x%02X, returned %d", reg, data[0], data[1], data[2],
                data[3], ret);
    return ret < 0 ? ret : 0;
}

static int cmd_write_register(const struct shell *shell, const int argc, const char *argv[]) {
    if (argc < 4) {
        shell_print(shell, "Usage: %s <device> 0x<register> 0x<data>", argv[0]);
        return -EINVAL;
    }

    char *endptr;
    const int device = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Invalid device number: %s", argv[1]);
        return -EINVAL;
    }

    if (device < 0 || device >= ARRAY_SIZE(adi_devs)) {
        shell_print(shell, "Invalid device number: %d", device);
        return -EINVAL;
    }

    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;

    const int reg = strtol(argv[2], &endptr, 16);
    if (*endptr != '\0') {
        shell_print(shell, "Invalid register: %s", argv[2]);
        return -EINVAL;
    }

    if (reg < 0 || reg > 0xFF) {
        shell_print(shell, "Invalid register: 0x%02X", reg);
        return -EINVAL;
    }

    union {
        uint32_t as_word;
        uint8_t as_bytes[4];
    } data;
    // Use `strtoll` here otherwise we run into sign errors.
    data.as_word = (uint32_t)strtoll(argv[3], &endptr, 16);
    if (*endptr != '\0') {
        shell_print(shell, "Invalid data: %s", argv[3]);
        return -EINVAL;
    }

    const int ret = api->write(dev, reg, data.as_bytes);

    shell_print(shell, "Wrote 0x%08X to 0x%02X, returned %d", data.as_word, reg, ret);
    return ret < 0 ? ret : 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(subcmds, SHELL_CMD_ARG(read, NULL, "Read a register", cmd_read_register, 3, 0),
                               SHELL_CMD_ARG(write, NULL, "Write a register", cmd_write_register, 4, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(adi_tmc2209, &subcmds, "TMC2209 register access", NULL);
