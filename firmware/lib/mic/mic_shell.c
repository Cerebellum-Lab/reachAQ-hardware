#include <stdio.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>

#include "mic.h"

static Microphone gMic;

static int cmd_init(const struct shell *shell, size_t argc, char **argv) {
    if (!mic_initialize(&gMic)) {
        shell_print(shell, "MIC initialization failed");
        return -1;
    }

    shell_print(shell, "MIC initialization complete.");

    return 0;
}

/* -------------------------------------------------------------------------- */

static int cmd_read(const struct shell *shell, size_t argc, char **argv) {
    void *buffer;
    uint32_t size;

    // Only call once if application is streaming, or call just before read()
    if (!mic_enable_reads(&gMic, true)) {
        return -1;
    }

    const int count = strtol(argv[1], NULL, 10);

    for (int i = 0; i < count; ++i) {
        if (!mic_read(&gMic, &buffer, &size)) {
            shell_print(shell, "Microphone read failed");

            return -1;
        }

        // do any type of processing here

        mic_release_buffer(buffer);
    }

    // Call only once at end of application if streaming, or call just after read()
    // and any processing.
    if (!mic_enable_reads(&gMic, false)) {
        return -1;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_mic, SHELL_CMD_ARG(init, NULL, "Initialize MIC", cmd_init, 0, 0),
                               SHELL_CMD_ARG(read, NULL, "Read from MIC", cmd_read, 2, 0), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(mic, &sub_mic, "MIC commands", NULL);
