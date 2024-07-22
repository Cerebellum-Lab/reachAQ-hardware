#pragma once
#include <zephyr/device.h>

/* Can technically go up to ~50kHz, but limiting to highest required frequency */
#define MAX_FREQUENCY 20000

/**
 * Generates a tone with the given frequency and duration on the provided tone generator.
 */
int ll_tone_generator_play_tone(const struct device *dev, unsigned int frequency_hz, unsigned int duration_ms);

/**
 * Generates a tone with the given frequency and duration on the provided
 * tone generator. Blocks for the duration of the tone.
 */
int ll_tone_generator_play_tone_blocking(const struct device *dev, unsigned int frequency_hz, unsigned int duration_ms);

/* Halts the tone generation proccess on the given tone generator */
int ll_tone_generator_abort_tone(const struct device *dev);