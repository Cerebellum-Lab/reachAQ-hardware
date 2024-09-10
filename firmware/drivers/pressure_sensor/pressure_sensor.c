#include "pressure_sensor.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_pressure_sensor

LOG_MODULE_REGISTER(ll_pressure_sensor, CONFIG_LL_PRESSURE_SENSOR_LOG_LEVEL);

/* Pressure Sensor configuration structure */
typedef struct {
    struct device *adc_dev;
    struct adc_channel_cfg adc_channel_cfg;
    struct adc_sequence sequence;
} ll_pressure_sensor_cfg_t;

/* Pressure Sensor data structure */
typedef struct {
    bool initialized;
    bool enabled;
    enum adc_action adc_action;
    volatile uint32_t raw_data;
} ll_pressure_sensor_data_t;

/* ADC callback */
static enum adc_action adc_cb(const struct device *dev, const struct adc_sequence *sequence, uint16_t sampling_index) {
    return *((enum adc_action *)sequence->options->user_data);
}

/* Initialize ADC */
static int ll_pressure_sensor_adc_init(const struct device *dev) {
    const ll_pressure_sensor_cfg_t *cfg = dev->config;

    int ret;

    if (!cfg->adc_dev) {
        LOG_ERR("ADC device not found");
        return -ENODEV;
    }

    /* Setup ADC channel */
    ret = adc_channel_setup(cfg->adc_dev, &cfg->adc_channel_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to configure ADC channel: %d", ret);
        return ret;
    }

    return ret;
}

/* Initialize Pressure Sensor */
static int ll_pressure_sensor_init(const struct device *dev) {
    ll_pressure_sensor_data_t *data = dev->data;
    LOG_INF("Initializing Pressure Sensor...");

    /* Initialize the ADC peripheral */
    int ret = ll_pressure_sensor_adc_init(dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize ADC: %d", ret);
        return PRESSURE_SENSOR_ADC_ERROR;
    }

    data->initialized = true;

    return ll_pressure_sensor_enable(dev);
}

/* Returns the last recorded pressure */
ll_pressure_sensor_error_t ll_pressure_sensor_get_pressure(const struct device *dev, uint16_t *value) {
    ll_pressure_sensor_data_t *data = dev->data;
    if (!data->initialized) {
        return PRESSURE_SENSOR_NOT_INITIALIZED;
    }

    if (!data->enabled) {
        return PRESSURE_SENSOR_NOT_ENABLED;
    }

    *value = ADC_COUNTS_TO_MV(data->raw_data);

    return PRESSURE_SENSOR_NO_ERROR;
}

/* Enables the given pressure sensor */
ll_pressure_sensor_error_t ll_pressure_sensor_enable(const struct device *dev) {
    const ll_pressure_sensor_cfg_t *cfg = dev->config;
    ll_pressure_sensor_data_t *data = dev->data;

    LOG_INF("Enabling pressure sensor...");

    if (!data->initialized) {
        LOG_ERR("Pressure sensor must be initialized to enable");
        return PRESSURE_SENSOR_NOT_INITIALIZED;
    }

    if (data->enabled) {
        LOG_ERR("Pressure sensor already enabled");
        return PRESSURE_SENSOR_ALREADY_ENABLED;
    }

    /* Set ADC action to repeat (continuous sampling) */
    data->adc_action = ADC_ACTION_REPEAT;

    /* Trigger asynchronous ADC sampling */
    int ret = adc_read_async(cfg->adc_dev, &cfg->sequence, NULL);
    if (ret != 0) {
        LOG_ERR("Failed to start ADC continuous conversion with DMA (err %d)", ret);
        return PRESSURE_SENSOR_ADC_ERROR;
    }

    data->enabled = true;

    LOG_INF("Pressure sensor enabled!");

    return PRESSURE_SENSOR_NO_ERROR;
}

/* Disables the given pressure sensor */
ll_pressure_sensor_error_t ll_pressure_sensor_disable(const struct device *dev) {
    ll_pressure_sensor_data_t *data = dev->data;
    LOG_INF("Disabling pressure sensor...");

    if (!data->initialized) {
        LOG_ERR("Pressure sensor must be initialized to disable");
        return PRESSURE_SENSOR_NOT_INITIALIZED;
    }

    if (!data->enabled) {
        LOG_ERR("Pressure sensor already disabled");
        return PRESSURE_SENSOR_ALREADY_DISABLED;
    }

    /* Set ADC action to finish (stop sampling) */
    data->adc_action = ADC_ACTION_FINISH;
    data->enabled = false;

    LOG_INF("Pressure sensor disabled!");

    return PRESSURE_SENSOR_NO_ERROR;
}

/* Convert frequency in hertz to period in microseconds */
#define FREQ_HZ_TO_PER_US(__freq__) (uint32_t)(1000000.0f / __freq__)

#define PRESSURE_SENSOR_INST(idx)                                                                                      \
    static ll_pressure_sensor_data_t pressure_sensor_data_##idx = {                                                    \
        .initialized = false,                                                                                          \
        .enabled = false,                                                                                              \
        .adc_action = ADC_ACTION_FINISH,                                                                               \
    };                                                                                                                 \
    static const struct adc_sequence_options pressure_sensor_sequence_opts_##idx = {                                   \
        .extra_samplings = 0,                                                                                          \
        .callback = adc_cb,                                                                                            \
        .user_data = &((pressure_sensor_data_##idx).adc_action),                                                       \
        .interval_us = FREQ_HZ_TO_PER_US(DT_INST_PROP(idx, sample_rate)),                                              \
    };                                                                                                                 \
    static const ll_pressure_sensor_cfg_t pressure_sensor_cfg_##idx = {                                                \
        .adc_dev = DEVICE_DT_GET(DT_INST_IO_CHANNELS_CTLR(idx)),                                                       \
        .adc_channel_cfg =                                                                                             \
            {                                                                                                          \
                .gain = ADC_GAIN_1,                                                                                    \
                .reference = ADC_REF_INTERNAL,                                                                         \
                .acquisition_time = ADC_ACQ_TIME_DEFAULT,                                                              \
                .channel_id = DT_INST_IO_CHANNELS_INPUT(idx),                                                          \
                .differential = 0,                                                                                     \
            },                                                                                                         \
        .sequence =                                                                                                    \
            {                                                                                                          \
                .channels = BIT(DT_INST_IO_CHANNELS_INPUT(idx)),                                                       \
                .buffer = &((pressure_sensor_data_##idx).raw_data),                                                    \
                .buffer_size = sizeof(uint16_t),                                                                       \
                .resolution = ADC_RESOLUTION,                                                                          \
                .options = &(pressure_sensor_sequence_opts_##idx),                                                     \
            },                                                                                                         \
    };                                                                                                                 \
                                                                                                                       \
    DEVICE_DT_INST_DEFINE(idx, ll_pressure_sensor_init, NULL, &pressure_sensor_data_##idx, &pressure_sensor_cfg_##idx, \
                          POST_KERNEL, CONFIG_LL_PRESSURE_SENSOR_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PRESSURE_SENSOR_INST)
