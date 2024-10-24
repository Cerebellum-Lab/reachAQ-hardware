#include <stdio.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "fft.h"
#include "generic_gpios.h"

static q31_t gFftBuffer[FFT_MAXIMUM_SAMPLE_SIZE * 2];

static q31_t gFftMagnitude[FFT_MAXIMUM_SAMPLE_SIZE];

void fft_timing_test(const int length) {
    const struct device *gpio = DEVICE_DT_GET_ANY(ll_generic_gpios);
    Fft fft;

    // On failure, signal any logic analyzer that the sequence is complete.
    if (!fft_initialize(&fft, length)) {
        ll_generic_gpio_write_pin_by_name(gpio, "CONT0", 1);
        ll_generic_gpio_write_pin_by_name(gpio, "CONT0", 0);
        return;
    }

    k_sched_lock();

    for (int i = 0; i < 5; ++i) {
        ll_generic_gpio_write_pin_by_name(gpio, "CONT0", 1);
        ll_generic_gpio_write_pin_by_name(gpio, "CONT0", 0);

        fft_process(&fft, gFftBuffer);

        ll_generic_gpio_write_pin_by_name(gpio, "CONT0", 1);

        fft_process_magnitude(&fft, gFftBuffer, gFftMagnitude);

        ll_generic_gpio_write_pin_by_name(gpio, "CONT0", 0);

        k_msleep(1);
    }

    k_sched_unlock();
}

/* -------------------------------------------------------------------------- */

static int cmd_fft_timing_tests(const struct shell *shell, size_t, char **argv) {
    char *end;

    shell_print(shell, "Running with size: %s\n", argv[1]);

    const int size = strtol(argv[1], &end, 10);
    if (*end != 0) {
        shell_print(shell, "Unable to convert argument to numeric value.");
        return -1;
    }

    fft_timing_test(size);

    return 0;
}

/* -------------------------------------------------------------------------- */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fft, SHELL_CMD_ARG(test, 0, "Usage: fft test <length>", cmd_fft_timing_tests, 2, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fft, &sub_fft, "FFT commands", NULL);
