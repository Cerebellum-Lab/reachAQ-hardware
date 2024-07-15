#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "servo.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *servo_devs[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DEV_GET_COMMA)};
static uint32_t servo_positions[ARRAY_SIZE(servo_devs)];

static int cmd_servo_set(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 3) {
        shell_print(shell, "Usage: %s <servo> <position>", argv[0]);
        return -EINVAL;
    }

    int servo = atoi(argv[1]);
    int position = atoi(argv[2]);

    if (servo < 0 || servo >= ARRAY_SIZE(servo_devs)) {
        shell_print(shell, "Invalid servo number");
        return -EINVAL;
    }

    if (position < 0 || position > 180) {
        shell_print(shell, "Invalid position");
        return -EINVAL;
    }

    servo_positions[servo] = position * 4000 / 180 + 1000;
    ll_queue_servo_positions(servo_devs[servo], &servo_positions[servo], sizeof(servo_positions[0]), K_FOREVER);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_servo,
    SHELL_CMD_ARG(set, NULL, "Set a servo position", cmd_servo_set, 3, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(servo, &sub_servo, "Servo commands", NULL);
