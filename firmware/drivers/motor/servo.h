#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>

int ll_queue_servo_positions(const struct device *dev, uint32_t *positions, size_t len, k_timeout_t timeout);
