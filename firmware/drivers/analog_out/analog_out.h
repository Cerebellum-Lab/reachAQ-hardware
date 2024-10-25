#pragma once
#include <zephyr/device.h>

/* Maximum commandable analog out output value in mV */
#define ANALOG_OUT_MAX_VALUE_MV 3300.0

/* Enumeration of analog output errors */
typedef enum {
    ANALOG_OUT_NO_ERROR = 0,
    ANALOG_OUT_NOT_INITIALIZED = 1,
    ANALOG_OUT_INVALID_CHANNEL = 2,
    ANALOG_OUT_INVALID_VALUE = 3,
} ll_analog_out_error_t;

/* Mapping of analog output errors to strings */
static const char *analog_out_error_to_str[] = {
    [ANALOG_OUT_NO_ERROR] = "No error",
    [ANALOG_OUT_NOT_INITIALIZED] = "Analog output not initialized",
    [ANALOG_OUT_INVALID_CHANNEL] = "Invalid channel parameter",
    [ANALOG_OUT_INVALID_VALUE] = "Invalid value parameter",
};

/* Writes the specified value in mV to the given analog output instance */
ll_analog_out_error_t ll_analog_out_write_value_mv(const struct device *dev, int value_mv);

/* Returns the current analog out output value in mv */
uint16_t ll_analog_out_get_value_mv(const struct device *dev);
