#include <stdbool.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "motor_callbacks.h"
#include "motor_motion.h"
#include "servo.h"

LOG_MODULE_REGISTER(motor_math, LOG_LEVEL_DBG);

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),

static const struct device *servo_devs[] = {DT_FOREACH_STATUS_OKAY(ll_servo, DEV_GET_COMMA)};
static float servo_positions[ARRAY_SIZE(servo_devs)] = {0};
static volatile bool servo_dma_moving[ARRAY_SIZE(servo_devs)] = {0};

void reset_bool(const struct device *dev, ll_motor_events_t event, void *arg, void *user_data) {
    LOG_DBG("Event %d", event);
    if (event == LL_MOTOR_EVENT_DMA_BLOCK_COMPLETE || event == LL_MOTOR_EVENT_DMA_QUEUE_EMPTY) {
        volatile bool *dma_moving = (volatile bool *)user_data;
        *dma_moving = false;
    }
    return;
}

static int cmd_servo_sinusoidal(const struct shell *shell, size_t argc, char **argv) {
    static bool first_run = true;
    int ret;
    if (argc < 3) {
        shell_print(shell, "Usage: %s <servo> <position>", argv[0]);
        return -EINVAL;
    }

    int servo = atoi(argv[1]);
    int position = atoi(argv[2]);
    float max_velocity = 600;      // 600 degrees per second
    float max_acceleration = 300;  // 300 degrees per second squared
    if (argc > 3) {
        max_velocity = atof(argv[3]);
        if (argc > 4) {
            max_acceleration = atof(argv[4]);
        }
    }

    if (servo < 0 || servo >= ARRAY_SIZE(servo_devs)) {
        shell_print(shell, "Invalid servo number");
        return -EINVAL;
    }

    if (position < 0 || position > 180) {
        shell_print(shell, "Invalid position");
        return -EINVAL;
    }

    static ll_servo_cb_t cb = {0};
    if (first_run) {
        first_run = false;

        cb.func = reset_bool;
        cb.user_data = &servo_dma_moving[servo], cb.node.next = NULL;

        ret = ll_servo_register_callback(servo_devs[servo], &cb);
        if (ret != 0) {
            shell_print(shell, "Failed to register callback");
            return -EINVAL;
        }
    }

    motor_motion_context_t motor_motion_context;
    ret = motor_motion_init_context_struct(servo_positions[servo], position, max_acceleration, max_velocity, 4000, 1000,
                                           &motor_motion_context);
    if (ret != 0) {
        shell_print(shell, "Failed to initialize motion context");
        return -EINVAL;
    }

    static int table[2][128] = {0};
    size_t table_index = 0;

    ssize_t table_size =
        motor_motion_generate_servo_displacement_table(table[table_index], ARRAY_SIZE(table[0]), &motor_motion_context);
    do {
        if (table_size < 0) {
            shell_print(shell, "Failed to generate servo displacement table");
            return -EINVAL;
        }
        
        if (table_size == 0) {
            break;
        }
        servo_dma_moving[servo] = true;
        ret = ll_queue_servo_positions(servo_devs[servo], table[table_index], table_size * sizeof(table[0][0]), K_FOREVER);
        if (ret != 0) {
            shell_print(shell, "Failed to queue servo positions");
            return -EINVAL;
        }
        LOG_DBG("queued %d positions", table_size);
        
        // While the motor is moving via DMA, go ahead and generate the next table.
        table_index = (table_index + 1) % 2;
        table_size = motor_motion_generate_servo_displacement_table(table[table_index], ARRAY_SIZE(table[0]), &motor_motion_context);

        while (servo_dma_moving[servo]) {
            k_sleep(K_MSEC(100));
        }
    } while (table_size > 0);

    servo_positions[servo] = position;

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_motor_math,
                               SHELL_CMD_ARG(sinusoidal, NULL, "Set a servo position sinusoidally",
                                             cmd_servo_sinusoidal, 3, 2),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(motor_math, &sub_motor_math, "Motor math motion commands", NULL);