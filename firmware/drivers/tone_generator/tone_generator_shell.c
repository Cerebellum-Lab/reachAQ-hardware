#include <stdlib.h>
#include <zephyr/shell/shell.h>

#include "tone_generator.h"

#define DEV_GET_COMMA(id) DEVICE_DT_GET(id),
static const struct device *tone_generator_devs[] = {DT_FOREACH_STATUS_OKAY(ll_tone_generator, DEV_GET_COMMA)};

static int cmd_tone_generator_play_tone(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 4) {
        shell_print(shell, "Usage: %s <tone_generator> <frequency> <duration_ms>", argv[0]);
        return -EINVAL;
    }

    int tone_generator = atoi(argv[1]);
    unsigned int frequency = atoi(argv[2]);
    unsigned int duration_ms = atoi(argv[3]);

    /* Make sure the specified tone generator exists */
    if (tone_generator < 0 || tone_generator >= ARRAY_SIZE(tone_generator_devs)) {
        shell_print(shell, "Invalid tone generator number");
        return -EINVAL;
    }

    /* Ensure that the frequency is between 1Hz and MAX_FREQUENCY Hz */
    if (frequency < 1 || frequency > MAX_FREQUENCY) {
        shell_print(shell, "Invalid frequency; expected [1...%d]", MAX_FREQUENCY);
        return -EINVAL;
    }

    /* Ensure that the duration is valid */
    if (duration_ms < 1) {
        shell_print(shell, "Invalid duration; expected [1...4,294,967,295]");
        return -EINVAL;
    }

    /* Play tone with the given parameters */
    int err;
    if ((err = ll_tone_generator_play_tone(tone_generator_devs[tone_generator], frequency, duration_ms))) {
        shell_print(shell, "Error playing tone: %d", err);
        return err;
    }

    return 0;
}

static int cmd_tone_generator_abort_tone(const struct shell *shell, size_t argc, char **argv) {
    if (argc != 2) {
        shell_print(shell, "Usage: %s <tone_generator>", argv[0]);
        return -EINVAL;
    }

    int tone_generator = atoi(argv[1]);

    /* Make sure the specified tone generator exists */
    if (tone_generator < 0 || tone_generator >= ARRAY_SIZE(tone_generator_devs)) {
        shell_print(shell, "Invalid tone generator number");
        return -EINVAL;
    }
    /* Play tone with the given parameters */
    int err;
    if ((err = ll_tone_generator_abort_tone(tone_generator_devs[tone_generator]))) {
        shell_print(shell, "Error aborting tone: %d", err);
        return err;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tone_generator,
                               SHELL_CMD_ARG(play_tone, NULL, "Play a tone", cmd_tone_generator_play_tone, 4, 0),
                               SHELL_CMD_ARG(abort_tone, NULL, "Abort a tone", cmd_tone_generator_abort_tone, 2, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tone_generator, &sub_tone_generator, "Servo commands", NULL);
