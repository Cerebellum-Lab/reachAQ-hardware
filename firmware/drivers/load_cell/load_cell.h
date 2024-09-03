#pragma once
#include <zephyr/device.h>

/* Returns the last read load value in millivolts as an int */
int16_t ll_load_cell_get_load_mv(const struct device *dev);

/* Returns the last read load value in millivolts as a float */
float ll_load_cell_get_load_mv_float(const struct device *dev);
