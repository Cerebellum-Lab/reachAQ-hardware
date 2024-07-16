#pragma once
#include <zephyr/device.h>

int ll_tone_generator_play_tone(const struct device *dev, unsigned int frequency_hz, unsigned int duration_ms);

int ll_tone_generator_play_tone_blocking(const struct device *dev, unsigned int frequency_hz, unsigned int duration_ms);

int ll_tone_generator_abort_tone(const struct device *dev);