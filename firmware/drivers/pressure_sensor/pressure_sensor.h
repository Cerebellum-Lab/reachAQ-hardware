#pragma once
#include <zephyr/device.h>

#define ADC_RESOLUTION 12

#define ADC_VOLTAGE_RANGE 3.3
#define UINT12_MAX 4096
#define VOLTAGE_TO_ADC_COUNTS(voltage) (uint16_t)(voltage * (UINT12_MAX / ADC_VOLTAGE_RANGE))

#define ADC_COUNTS_TO_MV(counts) (int32_t)((1000.0f * counts) / UINT12_MAX) * ADC_VOLTAGE_RANGE

#define MIN_PRESSURE_VOLTAGE 1.25
#define MAX_PRESSURE_VOLTAGE 2.5

#define MIN_PRESSURE_ADC_COUNTS VOLTAGE_TO_ADC_COUNTS(MIN_PRESSURE_VOLTAGE)
#define MAX_PRESSURE_ADC_COUNTS VOLTAGE_TO_ADC_COUNTS(MAX_PRESSURE_VOLTAGE)

enum {
    PRESSURE_SENSOR_NO_ERROR,
    PRESSURE_SENSOR_NOT_INITIALIZED,
    PRESSURE_SENSOR_NOT_ENABLED,
    PRESSURE_SENSOR_ALREADY_DISABLED,
    PRESSURE_SENSOR_ALREADY_ENABLED,
    PRESSURE_SENSOR_INVALID_INSTANCE,
};

int ll_pressure_sensor_get_pressure(const struct device *dev);

int ll_pressure_sensor_enable(const struct device *dev);

int ll_pressure_sensor_disable(const struct device *dev);