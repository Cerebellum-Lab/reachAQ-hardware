#pragma once
#include <zephyr/device.h>

/* Maximum commandable analog status output value in mV */
#define ANALOG_STATUS_MAX_VALUE_MV 3300.0

/* Enumeration of analog status errors */
typedef enum {
    ANALOG_STATUS_NO_ERROR = 0,
    ANALOG_STATUS_NOT_INITIALIZED = 1,
    ANALOG_STATUS_INVALID_CHANNEL = 2,
    ANALOG_STATUS_INVALID_VALUE = 3,
} ll_analog_status_error_t;

/* Mapping of analog status errors to strings */
static const char *analog_status_error_to_str[] = {
    [ANALOG_STATUS_NO_ERROR] = "No error",
    [ANALOG_STATUS_NOT_INITIALIZED] = "Analog status not initialized",
    [ANALOG_STATUS_INVALID_CHANNEL] = "Invalid channel parameter",
    [ANALOG_STATUS_INVALID_VALUE] = "Invalid value parameter",
};

/* Writes the specified value in mV to the given analog status instance */
ll_analog_status_error_t ll_analog_status_write_value_mv(const struct device *dev, int value_mv);
