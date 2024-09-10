#include <zephyr/shell/shell.h>

#include "pressure_sensor.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *pressure_sensor_devs[] = {DT_FOREACH_STATUS_OKAY(ll_pressure_sensor, DEV_GET_COMMA)};

static struct k_thread dump_thread;

/* How to determine a good stack size? */
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(dump_stack, STACK_SIZE);

/* Print the most recent pressure reading from the given pressure sensor, or the associated error, to the shell */
static void pressure_sensor_print_pressure_or_error(const struct shell *shell, const struct device *pressure_sensor,
                                                    int id) {
    uint16_t pressure;
    ll_pressure_sensor_error_t error = ll_pressure_sensor_get_pressure(pressure_sensor, &pressure);
    switch (error) {
        case PRESSURE_SENSOR_NO_ERROR:
            shell_print(shell, "pressure_sensor%d: %dmV", id, pressure);
            break;
        case PRESSURE_SENSOR_NOT_INITIALIZED:  /* fall-through */
        case PRESSURE_SENSOR_NOT_ENABLED:      /* fall-through */
        case PRESSURE_SENSOR_ALREADY_DISABLED: /* fall-through */
        case PRESSURE_SENSOR_ALREADY_ENABLED:  /* fall-through */
        case PRESSURE_SENSOR_INVALID_INSTANCE: /* fall-through */
        case PRESSURE_SENSOR_ADC_ERROR:
            shell_print(shell, "Error reading pressure_sensor%d: %s", id, pressure_sensor_error_to_str[error]);
            break;
        default:
            shell_print(shell, "Error reading pressure_sensor%d: Unknown error - %d", id, error);
            break;
    }
}

/* Print the most recent pressure reading from the given pressure sensor to the shell */
static int cmd_pressure_sensor_read(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: %s <pressure_sensor>", argv[0]);
        return -EINVAL;
    }

    int pressure_sensor = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (pressure_sensor < 0 || pressure_sensor >= ARRAY_SIZE(pressure_sensor_devs)) {
        shell_print(shell, "Invalid pressure sensor instance: %d", pressure_sensor);
        return -EINVAL;
    }

    pressure_sensor_print_pressure_or_error(shell, pressure_sensor_devs[pressure_sensor], pressure_sensor);

    return 0;
}

/* Thread target for asynchronous dumping of pressure data */
static void pressure_sensor_dump_thread(void *shell_ptr, void *arg2, void *arg3) {
    const struct shell *shell = shell_ptr;
    while (*((bool *)arg2)) {
        for (int i = 0; i < ARRAY_SIZE(pressure_sensor_devs); i++) {
            pressure_sensor_print_pressure_or_error(shell, pressure_sensor_devs[i], i);
        }
        // Short sleep here?
    }
}

/* Dump all pressure data from all present pressure sensors to the shell */
static int cmd_pressure_sensor_dump(const struct shell *shell, size_t argc, char **argv) {
    static bool dumping_enabled = false;
    if (argc != 1) {
        shell_print(shell, "Usage: %s", argv[0]);
        return -EINVAL;
    }

    int pressure_sensor = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (pressure_sensor < 0 || pressure_sensor >= ARRAY_SIZE(pressure_sensor_devs)) {
        shell_print(shell, "Invalid pressure sensor instance: %d", pressure_sensor);
        return -EINVAL;
    }

    if (dumping_enabled) {
        dumping_enabled = false;
        shell_print(shell, "Pressure sensor dumping disabled.");
    } else {
        dumping_enabled = true;
        shell_print(shell, "Pressure sensor dumping enabled.");

        k_thread_create(&dump_thread, dump_stack, K_THREAD_STACK_SIZEOF(dump_stack), pressure_sensor_dump_thread,
                        (void *)shell, (void *)&dumping_enabled, NULL, 7, 0, K_NO_WAIT);
    }

    return 0;
}

/* Enable the given pressure sensor */
static int cmd_pressure_sensor_enable(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: %s <pressure_sensor>", argv[0]);
        return -EINVAL;
    }

    int pressure_sensor = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (pressure_sensor < 0 || pressure_sensor >= ARRAY_SIZE(pressure_sensor_devs)) {
        shell_print(shell, "Invalid pressure_sensor instance: %d", pressure_sensor);
        return -EINVAL;
    }

    ll_pressure_sensor_error_t error = ll_pressure_sensor_enable(pressure_sensor_devs[pressure_sensor]);
    switch (error) {
        case PRESSURE_SENSOR_NO_ERROR:
            shell_print(shell, "Enabled pressure_sensor%d", pressure_sensor);
            break;
        case PRESSURE_SENSOR_NOT_INITIALIZED:  /* fall-through */
        case PRESSURE_SENSOR_NOT_ENABLED:      /* fall-through */
        case PRESSURE_SENSOR_ALREADY_DISABLED: /* fall-through */
        case PRESSURE_SENSOR_ALREADY_ENABLED:  /* fall-through */
        case PRESSURE_SENSOR_INVALID_INSTANCE: /* fall-through */
        case PRESSURE_SENSOR_ADC_ERROR:
            shell_print(shell, "Error enabling pressure_sensor%d: %s", pressure_sensor,
                        pressure_sensor_error_to_str[error]);
            break;
        default:
            shell_print(shell, "Error enabling pressure_sensor%d: Unknown error - %d", pressure_sensor, error);
            break;
    }

    return error;
}

/* Disable the given pressure sensor */
static int cmd_pressure_sensor_disable(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: %s <pressure_sensor>", argv[0]);
        return -EINVAL;
    }

    int pressure_sensor = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (pressure_sensor < 0 || pressure_sensor >= ARRAY_SIZE(pressure_sensor_devs)) {
        shell_print(shell, "Invalid pressure sensor instance: %d", pressure_sensor);
        return -EINVAL;
    }

    ll_pressure_sensor_error_t error = ll_pressure_sensor_disable(pressure_sensor_devs[pressure_sensor]);
    switch (error) {
        case PRESSURE_SENSOR_NO_ERROR:
            shell_print(shell, "Disabled pressure_sensor%d", pressure_sensor);
            break;
        case PRESSURE_SENSOR_NOT_INITIALIZED:  /* fall-through */
        case PRESSURE_SENSOR_NOT_ENABLED:      /* fall-through */
        case PRESSURE_SENSOR_ALREADY_DISABLED: /* fall-through */
        case PRESSURE_SENSOR_ALREADY_ENABLED:  /* fall-through */
        case PRESSURE_SENSOR_INVALID_INSTANCE: /* fall-through */
        case PRESSURE_SENSOR_ADC_ERROR:
            shell_print(shell, "Error disabling pressure_sensor%d: %s", pressure_sensor,
                        pressure_sensor_error_to_str[error]);
            break;
        default:
            shell_print(shell, "Error disabling pressure_sensor%d: Unknown error - %d", pressure_sensor, error);
            break;
    }

    return error;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_pressure_sensor, SHELL_CMD_ARG(read, NULL, "Read from a pressure sensor", cmd_pressure_sensor_read, 2, 0),
    SHELL_CMD_ARG(dump, NULL, "Dump all readings from all pressure sensors", cmd_pressure_sensor_dump, 1, 0),
    SHELL_CMD_ARG(enable, NULL, "Enable a pressure sensor", cmd_pressure_sensor_enable, 2, 0),
    SHELL_CMD_ARG(disable, NULL, "Disable a pressure sensor", cmd_pressure_sensor_disable, 2, 0), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pressure_sensor, &sub_pressure_sensor, "Pressure Sensor commands", NULL);
