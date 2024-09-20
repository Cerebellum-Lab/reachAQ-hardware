#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "pressure_sensor.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *pressure_sensor_devs[] = {DT_FOREACH_STATUS_OKAY(ll_pressure_sensor, DEV_GET_COMMA)};

/* Mapping of k_work_submit errors to string  */
static const char *k_work_submit_error_to_str[] = {
    [EBUSY] = "work item is cancelling; or queue is draining; or queue is plugged",
    [EINVAL] = "queue is null and the work item has never been run",
    [ENODEV] = "queue has not been started",
};

static void pressure_sensor_dump_handler(struct k_work *work);

typedef struct {
    bool initialized;
    struct k_work dump_work;
    bool dump_enabled;
    const struct shell *shell;
} dump_context_t;

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

static void pressure_sensor_dump_handler(struct k_work *work) {
    dump_context_t *dump_context = CONTAINER_OF(work, dump_context_t, dump_work);

    for (int i = 0; i < ARRAY_SIZE(pressure_sensor_devs); i++) {
        pressure_sensor_print_pressure_or_error(dump_context->shell, pressure_sensor_devs[i], i);
    }

    if (dump_context->dump_enabled) {
        /* Submit dump work item to the system workqueue */
        int ret = k_work_submit(work);
        switch (ret) {
            case 0: /* fall-through */
                /* work was already submitted to a queue */
            case 1: /* fall-through */
                /* work was not submitted and has been queued to queue */
            case 2:
                /* work was running and has been queued to the queue that was running it */
                break;
            case -EBUSY:  /* fall-through */
            case -EINVAL: /* fall-through */
            case -ENODEV:
                shell_print(dump_context->shell, "Failed to submit dump_work to the system workqueue: %s",
                            k_work_submit_error_to_str[-ret]);
                break;
            default:
                shell_print(dump_context->shell,
                            "Failed to submit dump_work to the system workqueue: Unknown error - %d", ret);
                break;
        }
    }
}

/* Dump all pressure data from all present pressure sensors to the shell */
static int cmd_pressure_sensor_dump(const struct shell *shell, size_t argc, char **argv) {
    static dump_context_t dump_context = {.initialized = false, .dump_enabled = false, .dump_work = {}, .shell = NULL};
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

    /* Make sure that the dump context is initialized */
    if (dump_context.initialized == false) {
        k_work_init(&dump_context.dump_work, pressure_sensor_dump_handler);
        dump_context.dump_enabled = false;
        dump_context.shell = shell;
        dump_context.initialized = true;
    }

    if (dump_context.dump_enabled) {
        dump_context.dump_enabled = false;
        shell_print(shell, "Pressure sensor dumping disabled.");
        return 0;
    }

    dump_context.dump_enabled = true;
    shell_print(shell, "Pressure sensor dumping enabled.");
    int ret = k_work_submit(&dump_context.dump_work);
    switch (ret) {
        case 0: /* fall-through */
            /* work was already submitted to a queue */
        case 1: /* fall-through */
            /* work was not submitted and has been queued to queue */
        case 2:
            /* work was running and has been queued to the queue that was running it */
            return 0;
            break;
        case -EBUSY:  /* fall-through */
        case -EINVAL: /* fall-through */
        case -ENODEV:
            shell_print(shell, "Failed to submit dump_work to the system workqueue: %s",
                        k_work_submit_error_to_str[-ret]);
            break;
        default:
            shell_print(shell, "Failed to submit dump_work to the system workqueue: Unknown error - %d", ret);
            break;
    }

    return ret;
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
