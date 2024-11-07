#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>

#include "microphone.h"

static int cmd_read(const struct shell *shell, size_t argc, char **argv) {
    static const struct device *devices = (const struct device *)DEVICE_DT_GET_ANY(ll_microphone);

    char *end = NULL;
    const int instance = strtol(argv[1], &end, 10);
    if (!end || *end != '\0') {
        shell_print(shell, "Usage: microphone read <mic #> <count>\n");
        return -1;
    }

    const struct device *device = &devices[instance];

    // Only call once if application is streaming, or call just before read()
    if (!ll_microphone_enable_reads(device, true)) {
        return -1;
    }

    end = NULL;
    const int count = strtol(argv[2], &end, 10);
    if (!end || *end != '\0') {
        shell_print(shell, "Usage: microphone read <mic #> <# samples>\n");
        return -1;
    }

    for (int i = 0; i < count; ++i) {
        uint32_t size;
        void *buffer = NULL;

        const MicReadResult rc = ll_microphone_read(device, &buffer, &size);
        if (MIC_READ_FAIL == rc) {
            shell_print(shell, "Microphone read failed");

            return -1;
        }

        if (MIC_READ_OK == rc) {
            // do any type of processing here
            ll_microphone_release_buffer(device, buffer);
        }
    }

    // Call only once at end of application if streaming, or call just after read()
    // and any processing.
    if (!ll_microphone_enable_reads(device, false)) {
        return -1;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_mic, SHELL_CMD_ARG(read, NULL, "Read from MIC", cmd_read, 3, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(mic, &sub_mic, "MIC commands", NULL);
