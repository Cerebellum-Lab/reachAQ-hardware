#include "generic_gpios.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_generic_gpios

LOG_MODULE_REGISTER(generic_gpio, CONFIG_GPIO_LOG_LEVEL);

typedef struct {
} ll_generic_gpio_data_t;

typedef struct {
    const struct gpio_dt_spec *inputs;
    const uint8_t num_inputs;
    const struct gpio_dt_spec *outputs;
    const uint8_t num_outputs;
} ll_generic_gpio_cfg_t;

int ll_generic_gpio_read_pin(const struct device *dev, uint8_t pin) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    // Bounds check that it's a valid pin
    if (pin >= cfg->num_inputs) {
        LOG_ERR("Invalid pin %d", pin);
        return -EINVAL;
    }

    // Read the specified pin
    const struct gpio_dt_spec *input = &cfg->inputs[pin];
    return gpio_pin_get_dt(input);
}

int ll_generic_gpio_read(const struct device *dev, uint32_t mask, uint32_t *value) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    *value = 0;
    for (int i = 0; i < cfg->num_inputs; i++) {
        if ((1 << i) & mask) {
            const struct gpio_dt_spec *input = &cfg->inputs[i];
            int ret = gpio_pin_get_dt(input);
            if (ret < 0) {
                LOG_ERR("Failed to read input %d", i);
                return ret;
            }
            *value |= (ret << i);
        }
    }

    return 0;
}

int ll_generic_gpio_read_all(const struct device *dev, uint32_t *value) {
    return ll_generic_gpio_read(dev, 0xFFFFFFFF, value);
}

int ll_generic_gpio_write_pin(const struct device *dev, uint8_t pin, uint8_t value) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    // Bounds check that it's a valid pin
    if (pin >= cfg->num_outputs) {
        LOG_ERR("Invalid pin %d", pin);
        return -EINVAL;
    }

    // Write the specified pin
    const struct gpio_dt_spec *output = &cfg->outputs[pin];
    return gpio_pin_set_dt(output, value);
}

int ll_generic_gpio_write(const struct device *dev, uint32_t mask, uint32_t value) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    for (int i = 0; i < cfg->num_outputs; i++) {
        if ((1 << i) & mask) {
            const struct gpio_dt_spec *output = &cfg->outputs[i];
            int ret = gpio_pin_set_dt(output, (value >> i) & 0x1);
            if (ret) {
                LOG_ERR("Failed to write output %d", i);
                return ret;
            }
        }
    }

    return 0;
}

int ll_generic_gpio_write_all(const struct device *dev, uint32_t value) {
    return ll_generic_gpio_write(dev, 0xFFFFFFFF, value);
}

int ll_generic_gpio_toggle_pin(const struct device *dev, uint8_t pin) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    // Bounds check that it's a valid pin
    if (pin >= cfg->num_outputs) {
        LOG_ERR("Invalid pin %d", pin);
        return -EINVAL;
    }

    // Toggle the specified pin
    const struct gpio_dt_spec *output = &cfg->outputs[pin];
    return gpio_pin_toggle_dt(output);
}

int ll_generic_gpio_toggle(const struct device *dev, uint32_t mask) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    for (int i = 0; i < cfg->num_outputs; i++) {
        if ((1 << i) & mask) {
            const struct gpio_dt_spec *output = &cfg->outputs[i];
            int ret = gpio_pin_toggle_dt(output);
            if (ret) {
                LOG_ERR("Failed to toggle output %d", i);
                return ret;
            }
        }
    }

    return 0;
}

int ll_generic_gpio_toggle_all(const struct device *dev) { return ll_generic_gpio_toggle(dev, 0xFFFFFFFF); }

static int ll_generic_gpio_init(const struct device *dev) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    // Initialize the outputs as outputs
    for (int i = 0; i < cfg->num_outputs; i++) {
        const struct gpio_dt_spec *output = &cfg->outputs[i];
        int ret = gpio_pin_configure_dt(output, GPIO_OUTPUT);
        if (ret) {
            LOG_ERR("Failed to configure output %d", i);
            return ret;
        }
    }

    // Initialize the inputs as inputs
    for (int i = 0; i < cfg->num_inputs; i++) {
        const struct gpio_dt_spec *input = &cfg->inputs[i];
        int ret = gpio_pin_configure_dt(input, GPIO_INPUT);
        if (ret) {
            LOG_ERR("Failed to configure input %d", i);
            return ret;
        }
    }

    return 0;
}

#define GENERIC_GPIO_INST(idx)                                                                                    \
    static const struct gpio_dt_spec input_dt_specs##idx[] = {                                                    \
        DT_INST_FOREACH_PROP_ELEM_SEP(idx, input_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, ))};                          \
                                                                                                                  \
    static const struct gpio_dt_spec output_dt_specs##idx[] = {                                                   \
        DT_INST_FOREACH_PROP_ELEM_SEP(idx, input_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, ))};                          \
                                                                                                                  \
    static ll_generic_gpio_data_t ll_generic_gpio_data##idx;                                                      \
    static const ll_generic_gpio_cfg_t ll_generic_gpio_cfg##idx = {                                               \
        .inputs = input_dt_specs##idx,                                                                            \
        .num_inputs = ARRAY_SIZE(input_dt_specs##idx),                                                            \
        .outputs = output_dt_specs##idx,                                                                          \
        .num_outputs = ARRAY_SIZE(output_dt_specs##idx),                                                          \
    };                                                                                                            \
                                                                                                                  \
    DEVICE_DT_INST_DEFINE(idx, ll_generic_gpio_init, NULL, &ll_generic_gpio_data##idx, &ll_generic_gpio_cfg##idx, \
                          POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(GENERIC_GPIO_INST)
