#pragma once
#include <zephyr/device.h>

uint16_t ll_pressure_sensor_get_pressure(const struct device *dev);
