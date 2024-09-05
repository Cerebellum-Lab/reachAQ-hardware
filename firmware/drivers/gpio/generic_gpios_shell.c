#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>

#include "generic_gpios.h"

static const struct device *gpio_dev = DEVICE_DT_GET_ANY(ll_generic_gpios);

/* Generic GPIOs read command */
static int cmd_generic_gpios_read(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: %s <pin_name>", argv[0]);
        return -EINVAL;
    }

    char *pin_name = argv[1];

    /* Read the pin and raise error if read fails */
    int result = ll_generic_gpio_read_pin_by_name(gpio_dev, pin_name);
    if (result < 0) {
        shell_print(shell, "Invalid pin name: <%s> does not exist", pin_name);
        return result;
    }

    /* Print result of read on success */
    shell_print(shell, "<%s>: %d", pin_name, result);

    return 0;
}

/* Generic GPIOs write command */
static int cmd_generic_gpios_write(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 3) {
        shell_print(shell, "Usage: %s <pin_name> <value>", argv[0]);
        return -EINVAL;
    }

    char *pin_name = argv[1];
    int value = atoi(argv[2]);

    /* Ensure that the specified value is valid */
    if (value != 0 && value != 1) {
        shell_print(shell, "Invalid value: <%d> - must be 0 or 1", value);
        return -EINVAL;
    }

    /* Write the pin and raise error if write fails */
    int result = ll_generic_gpio_write_pin_by_name(gpio_dev, pin_name, value);
    if (result < 0) {
        return result;
    }

    /* Print result of write on success */
    shell_print(shell, "<%s>: %d", pin_name, value);

    return 0;
}

/* Iterates over readable generic GPIOs */
static const char *generic_gpios_readable_lookup(size_t idx) {
    return ll_generic_gpio_lookup_readable_pin_name(gpio_dev, idx);
}

/* Provides writable generic GPIOs to the shell's tab-completion API */
static const char *generic_gpios_writable_lookup(size_t idx) {
    return ll_generic_gpio_lookup_writable_pin_name(gpio_dev, idx);
}

/* Provides readable generic GPIOs to the shell's tab-completion API */
static void generic_gpio_readable_name_get(size_t idx, struct shell_static_entry *entry) {
    entry->syntax = generic_gpios_readable_lookup(idx);
    entry->handler = NULL;
    entry->help = NULL;
    entry->subcmd = NULL;
}

/* Provides writable generic GPIOs to the shell's tab-completion API */
static void generic_gpio_writable_name_get(size_t idx, struct shell_static_entry *entry) {
    entry->syntax = generic_gpios_writable_lookup(idx);
    entry->handler = NULL;
    entry->help = NULL;
    entry->subcmd = NULL;
}

/* Create dynamic command for readable pin names */
SHELL_DYNAMIC_CMD_CREATE(dsub_generic_gpios_readable_name, generic_gpio_readable_name_get);

/* Create dynamic command for writable pin names */
SHELL_DYNAMIC_CMD_CREATE(dsub_generic_gpios_writable_name, generic_gpio_writable_name_get);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_generic_gpios,
                               SHELL_CMD_ARG(read, &dsub_generic_gpios_readable_name, "Read from a GPIO pin",
                                             cmd_generic_gpios_read, 2, 0),
                               SHELL_CMD_ARG(write, &dsub_generic_gpios_writable_name, "Write to a GPIO pin",
                                             cmd_generic_gpios_write, 3, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(generic_gpios, &sub_generic_gpios, "Generic GPIO commands", NULL);
