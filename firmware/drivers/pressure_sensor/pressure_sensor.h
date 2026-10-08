#pragma once
#include <zephyr/device.h>

uint16_t ll_pressure_sensor_get_pressure(const struct device *dev);

/* The latest completed sample and the k_cycle_get_64() time it completed, read as one consistent pair */
void ll_pressure_sensor_get_sample(const struct device *dev, uint16_t *value, uint64_t *sample_cycles);
