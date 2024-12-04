#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include "adi_tmc2209.h"
#include "adi_tmc2209_types.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *adi_devs[] = {DT_FOREACH_STATUS_OKAY(adi_tmc2209, DEV_GET_COMMA)};

static int dump_gconf(const struct shell *shell, const int device) {
    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;
    uint32_t val;

    // GCONF
    int ret = api->read(dev, REG_GCONF, (uint8_t *)&val);
    val = sys_be32_to_cpu(val);
    struct GCONF_data_fields *gconf = (struct GCONF_data_fields *)&val;
    shell_print(shell, "GCONF:");
    shell_print(shell, "  multistep_filt: %d", gconf->multistep_filt);
    shell_print(shell, "  test_mode: %d", gconf->test_mode_DO_NOT_USE);
    shell_print(shell, "  i_scale_analog: %d", gconf->i_scale_analog);
    shell_print(shell, "  internal_Rsense: %d", gconf->internal_Rsense);
    shell_print(shell, "  en_spreadcycle: %d", gconf->en_spreadcycle);
    shell_print(shell, "  shaft: %d", gconf->shaft);
    shell_print(shell, "  index_otpw: %d", gconf->index_otpw);
    shell_print(shell, "  index_step: %d", gconf->index_step);
    shell_print(shell, "  pdn_disable: %d", gconf->pdn_disable);
    shell_print(shell, "  mstep_reg_select: %d", gconf->mstep_reg_select);

    return ret;
}

static int dump_gstat(const struct shell *shell, const int device) {
    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;
    uint32_t val;

    // GSTAT
    int ret = api->read(dev, REG_GSTAT, (uint8_t *)&val);
    val = sys_be32_to_cpu(val);
    struct GSTAT_data_fields *gstat = (struct GSTAT_data_fields *)&val;
    shell_print(shell, "GSTAT:");
    shell_print(shell, "  reset: %d", gstat->reset);
    shell_print(shell, "  drv_err: %d", gstat->drv_err);
    shell_print(shell, "  uv_cp: %d", gstat->uv_cp);

    return ret;
}

static int dump_ifcnt(const struct shell *shell, const int device) {
    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;
    uint32_t val;

    // IFCNT
    int ret = api->read(dev, REG_IFCNT, (uint8_t *)&val);
    val = sys_be32_to_cpu(val);
    shell_print(shell, "IFCNT: %d", val);

    return ret;
}

static int dump_ihold_irun(const struct shell *shell, const int device) {
    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;
    uint32_t val;

    // IHOLD_IRUN
    int ret = api->read(dev, REG_IOIN, (uint8_t *)&val);
    val = sys_be32_to_cpu(val);
    struct IHOLD_IRUN_data_fields *ihold_irun = (struct IHOLD_IRUN_data_fields *)&val;
    shell_print(shell, "IHOLD_IRUN:");
    shell_print(shell, "  iholddelay: %d", ihold_irun->iholddelay);
    shell_print(shell, "  irun: %d", ihold_irun->irun);
    shell_print(shell, "  ihold: %d", ihold_irun->ihold);

    return ret;
}

static int dump_ioin(const struct shell *shell, const int device) {
    const struct device *dev = adi_devs[device];
    struct adi_tmc2209_driver_api *api = (struct adi_tmc2209_driver_api *)dev->api;
    uint32_t val;

    // IOIN
    int ret = api->read(dev, REG_IOIN, (uint8_t *)&val);
    val = sys_be32_to_cpu(val);
    struct IOIN_data_fields *ioin = (struct IOIN_data_fields *)&val;
    shell_print(shell, "IOIN:");
    shell_print(shell, "  enn: %d", ioin->enn);
    shell_print(shell, "  ms1: %d", ioin->ms1);
    shell_print(shell, "  ms2: %d", ioin->ms2);
    shell_print(shell, "  diag: %d", ioin->diag);
    shell_print(shell, "  pdn_uart: %d", ioin->pdn_uart);
    shell_print(shell, "  step: %d", ioin->step);
    shell_print(shell, "  spread_en: %d", ioin->spread_en);
    shell_print(shell, "  dir: %d", ioin->dir);
    shell_print(shell, "  version: 0x%x", ioin->version);

    return ret;
}

static int cmd_dump_registers(const struct shell *shell, const int argc, const char *argv[]) {
    if (argc < 2) {
        shell_print(shell, "Usage: %s <device>", argv[0]);
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

    dump_gconf(shell, device);
    dump_gstat(shell, device);
    dump_ifcnt(shell, device);
    dump_ihold_irun(shell, device);
    dump_ioin(shell, device);

    return 0;
}

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

    switch (reg) {
        case REG_GCONF:
            dump_gconf(shell, device);
            break;
        case REG_GSTAT:
            dump_gstat(shell, device);
            break;
        case REG_IFCNT:
            dump_ifcnt(shell, device);
            break;
        case REG_IHOLD_IRUN:
            dump_ihold_irun(shell, device);
            break;
        case REG_IOIN:
            dump_ioin(shell, device);
            break;
    }

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
                               SHELL_CMD_ARG(dump, NULL, "Dump all registers", cmd_dump_registers, 2, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(adi_tmc2209, &subcmds, "TMC2209 register access", NULL);
