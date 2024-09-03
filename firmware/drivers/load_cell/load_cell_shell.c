#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "load_cell.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *load_cell_devs[] = {DT_FOREACH_STATUS_OKAY(ll_load_cell, DEV_GET_COMMA)};

static struct k_thread dump_thread;

/* How to determine a good stack size? */
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(load_cell_dump_stack, STACK_SIZE);

/* Print the most recent pressure reading from the given load cell to the shell */
static int cmd_load_cell_read(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: %s <load_cell>", argv[0]);
        return -EINVAL;
    }

    int load_cell = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (load_cell < 0 || load_cell >= ARRAY_SIZE(load_cell_devs)) {
        shell_print(shell, "Invalid load cell instance: %d", load_cell);
        return -EINVAL;
    }

    float load = ll_load_cell_get_load_mv_float(load_cell_devs[load_cell]);
    shell_print(shell, "load_cell%d: %1.3fmV", load_cell,
                (double)load);  // Cast to double to avoid [-Wdouble-promotion]

    return 0;
}

/* Thread target for asynchronous dumping of pressure data */
static void load_cell_dump_thread(void *shell_ptr, void *arg2, void *arg3) {
    const struct shell *shell = shell_ptr;
    while (*((bool *)arg2)) {
        for (int i = 0; i < ARRAY_SIZE(load_cell_devs); i++) {
            float load = ll_load_cell_get_load_mv_float(load_cell_devs[i]);
            shell_print(shell, "load_cell%d: %1.3fmV", i,
                        (double)load);  // Cast to double to avoid [-Wdouble-promotion]
        }
        // Short sleep here?
    }
}

/* Dump all pressure data from all present load cells to the shell */
static int cmd_load_cell_dump(const struct shell *shell, size_t argc, char **argv) {
    static bool dumping_enabled = false;
    if (argc != 1) {
        shell_print(shell, "Usage: %s", argv[0]);
        return -EINVAL;
    }

    int load_cell = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (load_cell < 0 || load_cell >= ARRAY_SIZE(load_cell_devs)) {
        shell_print(shell, "Invalid load cell instance: %d", load_cell);
        return -EINVAL;
    }

    if (dumping_enabled) {
        dumping_enabled = false;
        shell_print(shell, "Load cell dumping disabled.");
    } else {
        dumping_enabled = true;
        shell_print(shell, "Load cell dumping enabled.");

        k_thread_create(&dump_thread, load_cell_dump_stack, K_THREAD_STACK_SIZEOF(load_cell_dump_stack),
                        load_cell_dump_thread, (void *)shell, (void *)&dumping_enabled, NULL, 7, 0, K_NO_WAIT);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_load_cell, SHELL_CMD_ARG(read, NULL, "Read from a load cell", cmd_load_cell_read, 2, 0),
    SHELL_CMD_ARG(dump, NULL, "Dump all readings from all load cells", cmd_load_cell_dump, 1, 0), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(load_cell, &sub_load_cell, "Load Cell commands", NULL);
