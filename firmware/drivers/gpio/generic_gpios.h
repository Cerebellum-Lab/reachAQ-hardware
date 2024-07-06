#pragma once

#include <zephyr/device.h>

int ll_generic_gpio_read_pin(const struct device* dev, uint8_t pin_num);
int ll_generic_gpio_read(const struct device* dev, uint32_t mask, uint32_t* value);
int ll_generic_gpio_read_all(const struct device* dev, uint32_t* value);

int ll_generic_gpio_write_pin(const struct device* dev, uint8_t pin_num, uint8_t value);
int ll_generic_gpio_write(const struct device* dev, uint32_t mask, uint32_t value);
int ll_generic_gpio_write_all(const struct device* dev, uint32_t value);

int ll_generic_gpio_toggle_pin(const struct device* dev, uint8_t pin_num);
int ll_generic_gpio_toggle(const struct device* dev, uint32_t mask);
int ll_generic_gpio_toggle_all(const struct device* dev);
