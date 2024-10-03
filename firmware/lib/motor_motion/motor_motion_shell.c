#include <stdbool.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "motor_motion.h"

LOG_MODULE_REGISTER(motor_math, LOG_LEVEL_DBG);

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),

static const struct device *const servo_devs[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DEV_GET_COMMA)};
static const struct device *const stepper_devs[] = {DT_FOREACH_STATUS_OKAY(ll_stepper, DEV_GET_COMMA)};

static int cmd_servo_set_pwm_parameters(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 4) {
        shell_print(shell, "Invalid number of arguments");
        return -EINVAL;
    }

    char *endptr;
    const int servo = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse servo number");
        return -EINVAL;
    }

    const int pwm_offset = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse pwm offset");
        return -EINVAL;
    }

    const int pwm_multiplier = strtol(argv[3], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse pwm multiplier");
        return -EINVAL;
    }

    if (servo < 0 || servo >= ARRAY_SIZE(servo_devs)) {
        shell_print(shell, "Invalid servo number");
        return -EINVAL;
    }

    const struct device *servo_dev = servo_devs[servo];

    const int ret = servo_set_parameters(servo_dev, 0.0f, 0.0f, pwm_offset, pwm_multiplier);
    if (ret != 0) {
        shell_print(shell, "Failed to set servo parameters: %d", ret);
        return -EINVAL;
    }

    return 0;
}

static int cmd_servo_set_physical_parameters(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 4) {
        shell_print(shell, "Invalid number of arguments");
        return -EINVAL;
    }

    char *endptr;
    const int servo = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse servo number");
        return -EINVAL;
    }

    const float max_velocity = strtof(argv[2], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse max velocity");
        return -EINVAL;
    }

    const float max_acceleration = strtof(argv[3], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse max acceleration");
        return -EINVAL;
    }

    if (servo < 0 || servo >= ARRAY_SIZE(servo_devs)) {
        shell_print(shell, "Invalid servo number");
        return -EINVAL;
    }

    const struct device *servo_dev = servo_devs[servo];

    const int ret = servo_set_parameters(servo_dev, max_velocity, max_acceleration, -1, -1);
    if (ret != 0) {
        shell_print(shell, "Failed to set servo parameters: %d", ret);
    }

    return 0;
}

static int cmd_servo_move(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 3) {
        shell_print(shell, "Invalid number of arguments");
        return -EINVAL;
    }

    char *endptr;
    const int servo = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse servo number");
        return -EINVAL;
    }

    if (servo < 0 || servo >= ARRAY_SIZE(servo_devs)) {
        shell_print(shell, "Invalid servo number");
        return -EINVAL;
    }

    const struct device *servo_dev = servo_devs[servo];

    const float position = strtof(argv[2], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse position");
        return -EINVAL;
    }

    const int ret = servo_move_to_position(servo_dev, position);

    if (ret != 0) {
        shell_print(shell, "Failed to move servo to position: %d", ret);
        return -EINVAL;
    }

    return 0;
}

static int cmd_stepper_set_steps(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 4) {
        shell_print(shell, "Invalid number of arguments");
        return -EINVAL;
    }

    char *endptr;
    const int stepper = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse stepper number");
        return -EINVAL;
    }

    const float min_step = strtof(argv[2], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse min step");
        return -EINVAL;
    }

    const float steps_per_revolution = strtof(argv[3], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse steps per revolution");
        return -EINVAL;
    }

    if (stepper < 0 || stepper >= ARRAY_SIZE(stepper_devs)) {
        shell_print(shell, "Invalid stepper number");
        return -EINVAL;
    }

    const struct device *const stepper_dev = stepper_devs[stepper];

    const int ret = stepper_set_parameters(stepper_dev, 0.0f, 0.0f, min_step, steps_per_revolution);

    if (ret != 0) {
        shell_print(shell, "Failed to set stepper parameters: %d", ret);
        return -EINVAL;
    }
    return 0;
}

static int cmd_stepper_set_physical_parameters(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 4) {
        shell_print(shell, "Invalid number of arguments");
        return -EINVAL;
    }
    char *endptr;
    const int stepper = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse stepper number");
        return -EINVAL;
    }

    const float max_velocity = strtof(argv[2], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse max velocity");
        return -EINVAL;
    }

    const float max_acceleration = strtof(argv[3], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse max acceleration");
        return -EINVAL;
    }

    if (stepper < 0 || stepper >= ARRAY_SIZE(stepper_devs)) {
        shell_print(shell, "Invalid stepper number");
        return -EINVAL;
    }

    const struct device *const stepper_dev = stepper_devs[stepper];

    const int ret = stepper_set_parameters(stepper_dev, max_velocity, max_acceleration, -1, -1);
    if (ret != 0) {
        shell_print(shell, "Failed to set stepper parameters: %d", ret);
        return -EINVAL;
    }
    return 0;
}

static int cmd_stepper_move(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 3) {
        shell_print(shell, "Invalid number of arguments");
        return -EINVAL;
    }
    char *endptr;
    const int stepper = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse stepper number");
        return -EINVAL;
    }

    const float position = strtof(argv[2], &endptr);
    if (*endptr != '\0') {
        shell_print(shell, "Couldn't parse position");
        return -EINVAL;
    }

    if (stepper < 0 || stepper >= ARRAY_SIZE(stepper_devs)) {
        shell_print(shell, "Invalid stepper number");
        return -EINVAL;
    }

    const struct device *const stepper_dev = stepper_devs[stepper];

    const int ret = stepper_move_to_position(stepper_dev, position);
    if (ret != 0) {
        shell_print(shell, "Failed to move stepper to position: %d", ret);
        return -EINVAL;
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_motor_math,
    SHELL_CMD_ARG(
        servo_set_pwm, NULL,
        "Set the PWM parameters (half-revolution multiple and offset) for a servo.\nUsage: servo_set_pwm <servo> "
        "<pwm_offset> <pwm_pulses_per_half_revolution>",
        cmd_servo_set_pwm_parameters, 4, 0),
    SHELL_CMD_ARG(servo_set_physical, NULL,
                  "Set the Physical parameters (max velocity and acceleration) for a servo.\nUsage: servo_set_physical "
                  "<servo> <max_velocity> <max_acceleration>",
                  cmd_servo_set_physical_parameters, 4, 0),
    SHELL_CMD_ARG(servo_move, NULL, "Set a servo position sinusoidally\nUsage: servo_move <servo> <position>",
                  cmd_servo_move, 3, 0),
    SHELL_CMD_ARG(
        stepper_set_steps, NULL,
        "Set the steps per revolution and minimum step for a stepper motor.\nUsage: stepper_set_steps <stepper> "
        "<min_step> <steps_per_revolution>",
        cmd_stepper_set_steps, 4, 0),
    SHELL_CMD_ARG(stepper_set_physical, NULL,
                  "Set the Physical parameters (max velocity and acceleration) for a stepper motor.\nUsage: "
                  "stepper_set_physical <stepper> <max_velocity> <max_acceleration>",
                  cmd_stepper_set_physical_parameters, 4, 0),
    SHELL_CMD_ARG(
        stepper_move, NULL,
        "Move a stepper motor to the specified position sinusoidally.\nUsage: stepper_move <stepper> <position",
        cmd_stepper_move, 3, 0),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(motor_math, &sub_motor_math, "Motor math motion commands", NULL);