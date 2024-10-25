#include <errno.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "analog_out.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *analog_out_devs[] = {DT_FOREACH_STATUS_OKAY(ll_analog_out, DEV_GET_COMMA)};

static int cmd_analog_out_write_value(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 3) {
        shell_error(shell, "Usage: %s <analog_out> <value_mv>", argv[0]);
        return -EINVAL;
    }

    int analog_out = atoi(argv[1]);
    int value_mv = atoi(argv[2]);

    /* Make sure the specified analog output exists */
    if (analog_out < 0 || analog_out >= ARRAY_SIZE(analog_out_devs)) {
        shell_error(shell, "Failed to write analog output value_mv: Invalid analog output instance - %d", analog_out);
        return -EINVAL;
    }

    /* Make sure the specified value is within the commandable range of outputs */
    if (value_mv < 0 || value_mv > ANALOG_OUT_MAX_VALUE_MV) {
        shell_error(shell, "Failed to write analog output value: %s - %dmV - must be in range [0.0, %1.1f]",
                    analog_out_error_to_str[ANALOG_OUT_INVALID_VALUE], value_mv, ANALOG_OUT_MAX_VALUE_MV);
        return -EINVAL;
    }

    /* Write the specified  in mv to the given analog output instance */
    ll_analog_out_error_t ret = ll_analog_out_write_value_mv(analog_out_devs[analog_out], value_mv);

    /* Print the resulting behavior */
    switch (ret) {
        case ANALOG_OUT_NO_ERROR:
            shell_print(shell, "<analog_out%d>: %dmV", analog_out, value_mv);
            break;
        case ANALOG_OUT_NOT_INITIALIZED:
            shell_error(shell, "Failed to write analog output value: %s", analog_out_error_to_str[ret]);
            return -ENODEV;
        case ANALOG_OUT_INVALID_CHANNEL:
            shell_error(shell, "Failed to write analog output value: %s", analog_out_error_to_str[ret]);
            return -EINVAL;
        case ANALOG_OUT_INVALID_VALUE:
            shell_error(shell, "Failed to write analog output value: %s - %dmV", analog_out_error_to_str[ret],
                        value_mv);
            return -EINVAL;
        default:
            shell_error(shell, "Failed to write analog output value: Unknown error - %d", ret);
            return -EIO;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_analog_out,
                               SHELL_CMD_ARG(write_value, NULL, "Write a value in mV to the analog out output",
                                             cmd_analog_out_write_value, 3, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(analog_out, &sub_analog_out, "Analog output commands", NULL);
