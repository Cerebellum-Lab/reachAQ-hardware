#pragma once

#include <zephyr/device.h>

const char* ll_generic_gpio_lookup_readable_pin_name(const struct device* dev, size_t idx);
const char* ll_generic_gpio_lookup_writable_pin_name(const struct device* dev, size_t idx);

int ll_generic_gpio_read_pin_by_name(const struct device* dev, const char* pin_name);
int ll_generic_gpio_read_pin(const struct device* dev, uint8_t pin_num);
int ll_generic_gpio_read(const struct device* dev, uint32_t mask, uint32_t* value);
int ll_generic_gpio_read_all(const struct device* dev, uint32_t* value);

int ll_generic_gpio_write_pin_by_name(const struct device* dev, const char* pin_name, uint8_t value);
int ll_generic_gpio_write_pin(const struct device* dev, uint8_t pin_num, uint8_t value);
int ll_generic_gpio_write(const struct device* dev, uint32_t mask, uint32_t value);
int ll_generic_gpio_write_all(const struct device* dev, uint32_t value);

int ll_generic_gpio_toggle_pin(const struct device* dev, uint8_t pin_num);
int ll_generic_gpio_toggle(const struct device* dev, uint32_t mask);
int ll_generic_gpio_toggle_all(const struct device* dev);

int ll_generic_gpio_register_state_change_handler(const struct device* dev, void (*callback)());
