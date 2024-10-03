#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "motor_callbacks.h"

typedef ll_motor_events_t ll_stepper_events_t;
typedef ll_motor_event_callback_t ll_stepper_event_callback_t;
typedef ll_motor_cb_t ll_stepper_cb_t;

typedef enum {
    LL_STEPPER_DIR_FORWARD,
    LL_STEPPER_DIR_BACKWARD,
} ll_stepper_dir_t;

int ll_queue_stepper_positions(const struct device *dev, uint32_t *positions, size_t len, k_timeout_t timeout);
int ll_stepper_register_callback(const struct device *dev, ll_stepper_cb_t *cb);
int ll_stepper_set_direction(const struct device *dev, ll_stepper_dir_t dir);
int ll_stepper_disable(const struct device *dev);
int ll_stepper_dma_stop(const struct device *dev);
