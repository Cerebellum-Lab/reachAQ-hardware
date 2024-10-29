#pragma once
#include <zephyr/device.h>

#define TONE_GENERATOR_MIN_FREQUENCY 1
#define TONE_GENERATOR_MAX_FREQUENCY 25500

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