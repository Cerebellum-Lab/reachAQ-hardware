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

/* Returns the amount time in ms remaining for the current tone */
uint32_t ll_tone_generator_get_time_remaining(const struct device *dev);

/* Returns the frequency in Hz of the current tone */
uint32_t ll_tone_generator_get_frequency(const struct device *dev);

/*
    board_clock_cycles() when the latest tone started; 0 if no tone has started. For a frequency that has a
    tone-output marker (5 and 6 kHz on the pellet) this is the marker edge on the STIM line the NI-DAQ records.
    For any other frequency there is no marker edge, and it is the start of playback.
*/
uint64_t ll_tone_generator_get_start_cycles(const struct device *dev);
