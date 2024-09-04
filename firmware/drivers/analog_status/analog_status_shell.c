#include <errno.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "analog_status.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *analog_status_devs[] = {DT_FOREACH_STATUS_OKAY(ll_analog_status, DEV_GET_COMMA)};

static int cmd_analog_status_write_value(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 3) {
        shell_error(shell, "Usage: %s <analog_status> <value_mv>", argv[0]);
        return -EINVAL;
    }

    int analog_status = atoi(argv[1]);
    int value_mv = atoi(argv[2]);

    /* Make sure the specified analog status exists */
    if (analog_status < 0 || analog_status >= ARRAY_SIZE(analog_status_devs)) {
        shell_error(shell, "Failed to write analog status value_mv: Invalid analog status instance - %d",
                    analog_status);
        return -EINVAL;
    }

    /* Make sure the specified value is within the commandable range of outputs */
    if (value_mv < 0 || value_mv > ANALOG_STATUS_MAX_VALUE_MV) {
        shell_error(shell, "Failed to write analog status value: %s - %dmV - must be in range [0.0, %1.1f]",
                    analog_status_error_to_str[ANALOG_STATUS_INVALID_VALUE], value_mv, ANALOG_STATUS_MAX_VALUE_MV);
        return -EINVAL;
    }

    /* Write the specified  in mv to the given analog status instance */
    ll_analog_status_error_t ret = ll_analog_status_write_value_mv(analog_status_devs[analog_status], value_mv);

    /* Print the resulting behavior */
    switch (ret) {
        case ANALOG_STATUS_NO_ERROR:
            shell_print(shell, "<analog_status%d>: %dmV", analog_status, value_mv);
            break;
        case ANALOG_STATUS_NOT_INITIALIZED:
            shell_error(shell, "Failed to write analog status value: %s", analog_status_error_to_str[ret]);
            return -ENODEV;
        case ANALOG_STATUS_INVALID_CHANNEL:
            shell_error(shell, "Failed to write analog status value: %s", analog_status_error_to_str[ret]);
            return -EINVAL;
        case ANALOG_STATUS_INVALID_VALUE:
            shell_error(shell, "Failed to write analog status value: %s - %dmV", analog_status_error_to_str[ret],
                        value_mv);
            return -EINVAL;
        default:
            shell_error(shell, "Failed to write analog status value: Unknown error - %d", ret);
            return -EIO;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_analog_status,
                               SHELL_CMD_ARG(write_value, NULL, "Write a value in mV to the analog status output",
                                             cmd_analog_status_write_value, 3, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(analog_status, &sub_analog_status, "Analog status commands", NULL);
