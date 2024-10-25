#include "generic_gpios.h"

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_generic_gpios

LOG_MODULE_REGISTER(ll_generic_gpios, CONFIG_LL_GENERIC_GPIO_LOG_LEVEL);

#define MAX_NAME_LENGTH 32

/*
This typedef defines a pointer to an array of fixed-length strings (char arrays),
where each array is of size MAX_NAME_LENGTH. The typedef `pin_names_t` represents
a pointer to the first element of such an array of fixed-size strings, which allows
efficient storage and access of multiple fixed-length strings without dynamic memory
allocation - in particular, it allows me to define the number of fixed-size strings on
a per-instance basis, based on the number of *-gpios provided through the devicetree.

Purpose:
- `pin_names_t` is used in the `ll_generic_gpio_data_t` struct to represent arrays
  of valid input and output pin names. The struct `ll_generic_gpio_data_t` stores
  these names and their respective counts, allowing easy access to input and output
  pin names while maintaining fixed-size memory allocation.

+-------------------+
|   pin_names_t     |  -->  +-----------------------+
+-------------------+       |  char array (32 chars) |
                            +-----------------------+
                            |  char array (32 chars) |
                            +-----------------------+
                            |  char array (32 chars) |
                            +-----------------------+
                            |        ...            |
                            +-----------------------+
*/
typedef char (*pin_names_t)[MAX_NAME_LENGTH];

typedef struct {
    pin_names_t valid_input_names;
    pin_names_t valid_output_names;
    uint8_t num_valid_input_names;
    uint8_t num_valid_output_names;
    struct gpio_callback state_change_callback;
    void (*state_change_handler)(void);
} ll_generic_gpio_data_t;

typedef struct {
    const struct gpio_dt_spec *inputs;
    const char **input_names;
    const uint8_t num_inputs;
    const uint8_t num_input_names;
    const struct gpio_dt_spec *outputs;
    const char **output_names;
    const uint8_t num_outputs;
    const uint8_t num_output_names;
} ll_generic_gpio_cfg_t;

/* Returns the number of readable pins (inputs and outputs) */
static const int ll_generic_gpio_get_num_readable_pins(const struct device *dev) {
    ll_generic_gpio_data_t *data = dev->data;

    return data->num_valid_input_names + data->num_valid_output_names;
}

/* Returns the number of writable pins (outputs) */
static const int ll_generic_gpio_get_num_writable_pins(const struct device *dev) {
    ll_generic_gpio_data_t *data = dev->data;

    return data->num_valid_output_names;
}

/* Returns array of readable pins (inputs and outputs) */
static const struct gpio_dt_spec *ll_generic_gpio_get_readable_pins(const struct device *dev) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    /* inputs and outputs are contiguous arrays, so this is valid */
    return cfg->inputs;
}

/*
    UNUSED due to implementation specifics - should I remove this?
*/
/* Returns array of writable pins (outputs) */
__attribute__((unused)) static const struct gpio_dt_spec *ll_generic_gpio_get_writable_pins(const struct device *dev) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;

    return cfg->outputs;
}

/* Returns array of readable pin names (inputs and outputs) */
static const pin_names_t ll_generic_gpio_get_readable_pin_names(const struct device *dev) {
    ll_generic_gpio_data_t *data = dev->data;

    /* input_names and output_names are contiguous arrays, so this is valid */
    return data->valid_input_names;
}

/* Returns array of writable pins (inputs and outputs) */
static const pin_names_t ll_generic_gpio_get_writable_pin_names(const struct device *dev) {
    ll_generic_gpio_data_t *data = dev->data;

    return data->valid_output_names;
}

/* Returns readable pin name at the specified index, if it exists (inputs and outputs) */
const char *ll_generic_gpio_lookup_readable_pin_name(const struct device *dev, size_t idx) {
    ll_generic_gpio_data_t *data = dev->data;

    if (idx >= ll_generic_gpio_get_num_readable_pins(dev)) {
        return NULL;
    }

    /* input_names and output_names are contiguous arrays, so this is valid */
    return data->valid_input_names[idx];
}

/* Returns writable pin name at the specified index, if it exists (inputs and outputs) */
const char *ll_generic_gpio_lookup_writable_pin_name(const struct device *dev, size_t idx) {
    ll_generic_gpio_data_t *data = dev->data;

    if (idx >= ll_generic_gpio_get_num_writable_pins(dev)) {
        return NULL;
    }

    return data->valid_output_names[idx];
}

/*
    Searches the given pin_names array for the specified pin_name.
    Returns the index of the pin on success, or -1 if the pin does
    not exist in pin_names.
*/
static int ll_generic_gpio_get_idx_from_name(size_t count, pin_names_t pin_names, const char *pin_name) {
    int pin_idx = 0;
    /* Find index of pin name in pin names array, if present */
    while (pin_idx < count && strncmp(pin_name, pin_names[pin_idx], MAX_NAME_LENGTH - 1) != 0) {
        pin_idx++;
    }

    /* If the specified pin name is not present in pin names, return -1, else return the index */
    if (pin_idx >= count) {
        return -1;
    } else {
        return pin_idx;
    }
}

int ll_generic_gpio_read_pin_by_name(const struct device *dev, const char *pin_name) {
    const pin_names_t readable_pin_names = ll_generic_gpio_get_readable_pin_names(dev);
    const struct gpio_dt_spec *readable_pins = ll_generic_gpio_get_readable_pins(dev);
    const int num_readable_pins = ll_generic_gpio_get_num_readable_pins(dev);

    /* Search readable pin names for the specified pin name */
    int pin_idx = ll_generic_gpio_get_idx_from_name(num_readable_pins, readable_pin_names, pin_name);

    /* Raise error if pin does not exist */
    if (pin_idx < 0) {
        LOG_ERR("Invalid pin name: <%s> does not exist", pin_name);
        return -EINVAL;
    }

    /* Read the specified pin explicitly (to allow for reading inputs and outputs)*/
    const struct gpio_dt_spec *pin = &readable_pins[pin_idx];
    return gpio_pin_get_dt(pin);
}

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

    int new_value = 0;
    for (int i = 0; i < cfg->num_inputs; i++) {
        if ((1 << i) & mask) {
            const struct gpio_dt_spec *input = &cfg->inputs[i];
            int ret = gpio_pin_get_dt(input);
            if (ret < 0) {
                LOG_ERR("Failed to read input %d", i);
                return ret;
            }
            new_value |= (ret << i);
        }
    }

    *value = new_value;

    return 0;
}

int ll_generic_gpio_read_all(const struct device *dev, uint32_t *value) {
    return ll_generic_gpio_read(dev, 0xFFFFFFFF, value);
}

int ll_generic_gpio_write_pin_by_name(const struct device *dev, const char *pin_name, uint8_t value) {
    const pin_names_t writable_pin_names = ll_generic_gpio_get_writable_pin_names(dev);
    const int num_writable_pins = ll_generic_gpio_get_num_writable_pins(dev);

    /* Search writable pin names for the specified pin name */
    int pin_idx = ll_generic_gpio_get_idx_from_name(num_writable_pins, writable_pin_names, pin_name);

    /* If pin is not found in outputs, print associated errors */
    if (pin_idx < 0) {
        ll_generic_gpio_data_t *data = dev->data;
        const pin_names_t readable_pin_names = ll_generic_gpio_get_readable_pin_names(dev);
        const int num_input_names = data->num_valid_input_names;

        /* Search input pin names for the specified pin name */
        pin_idx = ll_generic_gpio_get_idx_from_name(num_input_names, readable_pin_names, pin_name);

        /* Error arbitration */
        if (pin_idx < 0) {
            /* If the specified name belongs to no pin, raise invalid argument error */
            LOG_ERR("Invalid pin name: <%s> does not exist", pin_name);
            return -EINVAL;
        } else {
            /* If the specified name belongs to an input pin, raise operation not supported error */
            LOG_ERR("Cannot write to an input pin");
            return -EOPNOTSUPP;
        }
    }

    /* Write the specified pin */
    return ll_generic_gpio_write_pin(dev, pin_idx, value);
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

/* Helper function to generate unique names and handle duplicates */
static void ll_generic_gpio_generate_unique_name(const char *base_name, int occurrence, char result[MAX_NAME_LENGTH]) {
    if (occurrence == 0) {
        snprintf(result, MAX_NAME_LENGTH, "%s", base_name);
    } else {
        snprintf(result, MAX_NAME_LENGTH, "%s(%d)", base_name, occurrence);
    }
}

/* Function to count occurrences of a name across input and output arrays */
static int ll_generic_gpio_count_name_occurrences(const pin_names_t names, int size, const char *name) {
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (strncmp(names[i], name, MAX_NAME_LENGTH - 1) == 0) {
            count++;
        }
    }
    return count;
}

static int ll_generic_gpio_configure_pins(const struct gpio_dt_spec *pins, size_t count, gpio_flags_t direction) {
    if (direction != GPIO_INPUT && direction != GPIO_OUTPUT) {
        LOG_ERR("Failed to configure pin: direction must be GPIO_INPUT or GPIO_OUTPUT");
        return -EINVAL;
    }

    /* Initialize the given pins with the specified direction */
    for (int i = 0; i < count; i++) {
        const struct gpio_dt_spec *pin = &pins[i];
        int ret = gpio_pin_configure_dt(pin, direction);
        if (ret) {
            if (direction == GPIO_INPUT) {
                LOG_ERR("Failed to configure input %d: %d", i, ret);
            } else {
                LOG_ERR("Failed to configure output %d: %d", i, ret);
            }
            return ret;
        }
    }

    return 0;
}

/*
    Performs run-time *-gpio-names validation, and populates the valid_*_names
    array in ll_generic_gpio_data_t with the "valid" forms of the provided names:
        - Base Case (name is already valid): <valid name> = <name>
        - Name exceeds MAX_NAME_LENGTH:      <valid name> = <name>[0:MAX_NAME_LENGTH] (truncated)
        - Duplicate name (Nth occurence):    <valid name> = <name>(N)
        - No name provided (Nth occurence):  <valid name> = unnamed_pin_N
*/
static void ll_generic_gpio_populate_valid_names(const struct device *dev) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;
    ll_generic_gpio_data_t *data = dev->data;

    int unnamed_pin_count = 0;

    /* Fill valid input names array with names, handling missing and duplicate names */
    int valid_input_count = 0;
    for (int i = 0; i < cfg->num_inputs; i++) {
        if (i < cfg->num_input_names && cfg->input_names[i] && cfg->input_names[i][0] != '\0') {
            size_t len = strlen(cfg->input_names[i]);
            if (len >= MAX_NAME_LENGTH) {
                LOG_WRN("Length of pin name (%d) exceeds MAX_NAME_LENGTH (%d) and will be truncated: '%s'", len,
                        MAX_NAME_LENGTH, cfg->input_names[i]);
            }
            int occurrence =
                ll_generic_gpio_count_name_occurrences(data->valid_input_names, valid_input_count, cfg->input_names[i]);
            ll_generic_gpio_generate_unique_name(cfg->input_names[i], occurrence,
                                                 data->valid_input_names[valid_input_count]);
            if (occurrence > 0) {
                LOG_WRN("Duplicate pin name detected - using '%s'", data->valid_input_names[valid_input_count]);
            }
        } else {
            snprintf(data->valid_input_names[valid_input_count], MAX_NAME_LENGTH, "unnamed_input_%d",
                     unnamed_pin_count++);
            LOG_WRN("Unnamed input pin detected - using '%s'", data->valid_input_names[valid_input_count]);
        }

        valid_input_count++;
    }

    for (int i = valid_input_count; i < cfg->num_input_names; i++) {
        LOG_WRN("Unused input pin name detected: '%s'", cfg->input_names[i]);
    }

    data->num_valid_input_names = valid_input_count;

    /* Fill valid output names array with names, handling missing and duplicate names */
    int valid_output_count = 0;
    for (int i = 0; i < cfg->num_outputs; i++) {
        if (i < cfg->num_output_names && cfg->output_names[i] && cfg->output_names[i][0] != '\0') {
            size_t len = strlen(cfg->output_names[i]);
            if (len >= MAX_NAME_LENGTH) {
                LOG_WRN("Length of pin name (%d) exceeds MAX_NAME_LENGTH (%d) and will be truncated: '%s'", len,
                        MAX_NAME_LENGTH, cfg->output_names[i]);
            }
            int occurrence = ll_generic_gpio_count_name_occurrences(data->valid_output_names, valid_output_count,
                                                                    cfg->output_names[i]);
            ll_generic_gpio_generate_unique_name(cfg->output_names[i], occurrence,
                                                 data->valid_output_names[valid_output_count]);
            if (occurrence > 0) {
                LOG_WRN("Duplicate pin name detected - using '%s'", data->valid_output_names[valid_output_count]);
            }
        } else {
            snprintf(data->valid_output_names[valid_output_count], MAX_NAME_LENGTH, "unnamed_output_%d",
                     unnamed_pin_count++);
            LOG_WRN("Unnamed output pin detected - using '%s'", data->valid_output_names[valid_output_count]);
        }
        valid_output_count++;
    }

    for (int i = valid_output_count; i < cfg->num_output_names; i++) {
        LOG_WRN("Unused output pin name detected: '%s'", cfg->output_names[i]);
    }

    data->num_valid_output_names = valid_output_count;

    LOG_INF("GPIO names validated and mapped successfully");
}

/* ISR to be called on state change, if a state change handler has been enabled  */
static void ll_generic_gpio_state_change_isr(const struct device *port, struct gpio_callback *cb,
                                             gpio_port_pins_t pins) {
    ll_generic_gpio_data_t *data = CONTAINER_OF(cb, ll_generic_gpio_data_t, state_change_callback);

    /* Confirm that the registered state change handler is not NULL before calling */
    if (data->state_change_handler != NULL) {
        data->state_change_handler();
    }
}

int ll_generic_gpio_register_state_change_handler(const struct device *dev, void (*handler)()) {
    ll_generic_gpio_data_t *data = dev->data;

    /* Assign the state change handler to be called by the ISR */
    data->state_change_handler = handler;

    /* Grab all pins (readable includes inputs and outputs) */
    uint8_t pin_count = ll_generic_gpio_get_num_readable_pins(dev);
    const struct gpio_dt_spec *pins = ll_generic_gpio_get_readable_pins(dev);

    /* Enable the interrupt on each pin */
    int ret;
    for (int i = 0; i < pin_count; i++) {
        ret = gpio_pin_interrupt_configure_dt(&pins[i], GPIO_INT_ENABLE | GPIO_INT_EDGE_BOTH);
        if (ret != 0) {
            LOG_ERR("Failed to register state change handler: Error configuring interrupt for pin <%s> - %d",
                    ll_generic_gpio_lookup_readable_pin_name(dev, i), ret);
            return ret;
        }
    }

    return 0;
}

static int ll_generic_gpio_init(const struct device *dev) {
    const ll_generic_gpio_cfg_t *cfg = dev->config;
    ll_generic_gpio_data_t *data = dev->data;
    int ret;

    /* Initialize the outputs as outputs */
    ret = ll_generic_gpio_configure_pins(cfg->outputs, cfg->num_outputs, GPIO_OUTPUT);
    if (ret != 0) {
        LOG_ERR("Failed to initialize generic GPIO: Error configuring output pins - %d", ret);
        return ret;
    }

    /* Initialize the inputs as inputs */
    ret = ll_generic_gpio_configure_pins(cfg->inputs, cfg->num_inputs, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to initialize generic GPIO: Error configuring input pins - %d", ret);
        return ret;
    }

    /*
        Populate valid_input_names and valid_output_names with the
        "valid" forms of the names provided in the device tree.
    */
    ll_generic_gpio_populate_valid_names(dev);

    uint8_t pin_count = ll_generic_gpio_get_num_readable_pins(dev);
    const struct gpio_dt_spec *pins = ll_generic_gpio_get_readable_pins(dev);

    gpio_port_pins_t pin_mask = 0;
    for (int i = 0; i < pin_count; i++) {
        pin_mask |= BIT(pins[i].pin);
    }

    /* Setup interrupts for state change but leave interrupts disabled */
    gpio_init_callback(&data->state_change_callback, ll_generic_gpio_state_change_isr, pin_mask);
    for (int i = 0; i < pin_count; i++) {
        ret = gpio_add_callback_dt(&pins[i], &data->state_change_callback);
        if (ret != 0) {
            LOG_ERR("Failed to register state change handler: Error adding callback for pin <%s> - %d",
                    ll_generic_gpio_lookup_readable_pin_name(dev, i), ret);
            return ret;
        }
    }

    return 0;
}

#define GENERIC_GPIO_INST(idx)                                                                                    \
    static const struct gpio_dt_spec pin_dt_specs##idx[] = {                                                      \
        DT_INST_FOREACH_PROP_ELEM_SEP(idx, input_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, )),                           \
        DT_INST_FOREACH_PROP_ELEM_SEP(idx, output_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, )),                          \
    };                                                                                                            \
                                                                                                                  \
    static const char *pin_names##idx[] = {                                                                       \
        DT_INST_FOREACH_PROP_ELEM_SEP(idx, input_gpio_names, DT_PROP_BY_IDX, (, )),                               \
        DT_INST_FOREACH_PROP_ELEM_SEP(idx, output_gpio_names, DT_PROP_BY_IDX, (, )),                              \
    };                                                                                                            \
                                                                                                                  \
    char valid_pin_names##idx[DT_INST_PROP_LEN(idx, input_gpios) + DT_INST_PROP_LEN(idx, output_gpios)]           \
                             [MAX_NAME_LENGTH];                                                                   \
                                                                                                                  \
    static ll_generic_gpio_data_t ll_generic_gpio_data##idx = {                                                   \
        .valid_input_names = valid_pin_names##idx,                                                                \
        .valid_output_names = &valid_pin_names##idx[DT_INST_PROP_LEN(idx, input_gpios)],                          \
        .num_valid_input_names = 0,                                                                               \
        .num_valid_output_names = 0,                                                                              \
        .state_change_handler = NULL,                                                                             \
    };                                                                                                            \
    static const ll_generic_gpio_cfg_t ll_generic_gpio_cfg##idx = {                                               \
        .inputs = pin_dt_specs##idx,                                                                              \
        .input_names = pin_names##idx,                                                                            \
        .num_inputs = DT_INST_PROP_LEN(idx, input_gpios),                                                         \
        .num_input_names = DT_INST_PROP_LEN(idx, input_gpio_names),                                               \
        .outputs = &(pin_dt_specs##idx[DT_INST_PROP_LEN(idx, input_gpios)]),                                      \
        .output_names = &(pin_names##idx[DT_INST_PROP_LEN(idx, input_gpio_names)]),                               \
        .num_outputs = DT_INST_PROP_LEN(idx, output_gpios),                                                       \
        .num_output_names = DT_INST_PROP_LEN(idx, output_gpio_names),                                             \
    };                                                                                                            \
                                                                                                                  \
    DEVICE_DT_INST_DEFINE(idx, ll_generic_gpio_init, NULL, &ll_generic_gpio_data##idx, &ll_generic_gpio_cfg##idx, \
                          POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(GENERIC_GPIO_INST)
