#include "pressure_sensor.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_pressure_sensor

LOG_MODULE_REGISTER(ll_pressure_sensor, CONFIG_LL_PRESSURE_SENSOR_LOG_LEVEL);

#define ADC_RESOLUTION 12

/* Pressure Sensor configuration structure */
typedef struct {
    const struct device *adc_dev;
    struct adc_channel_cfg adc_channel_cfg;
    struct adc_sequence sequence;
} ll_pressure_sensor_cfg_t;

/* Pressure Sensor data structure */
typedef struct {
    uint16_t raw_data;
} ll_pressure_sensor_data_t;

static int ll_pressure_sensor_enable(const struct device *dev);

/* ADC callback */
static enum adc_action adc_cb(const struct device *dev, const struct adc_sequence *, uint16_t) {
    return ADC_ACTION_REPEAT;
}

static int ll_pressure_sensor_adc_init(const struct device *dev) {
    const ll_pressure_sensor_cfg_t *cfg = dev->config;

    /* Setup ADC channel */
    const int ret = adc_channel_setup(cfg->adc_dev, &cfg->adc_channel_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to configure ADC channel: %d", ret);
    }

    return ret;
}

/* Initialize Pressure Sensor */
static int ll_pressure_sensor_init(const struct device *dev) {
    LOG_INF("Initializing Pressure Sensor...");

    /* Initialize the ADC peripheral */
    int ret = ll_pressure_sensor_adc_init(dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize ADC: %d", ret);
        return ret;
    }

    return ll_pressure_sensor_enable(dev);
}

/* Returns the last recorded pressure */
uint16_t ll_pressure_sensor_get_pressure(const struct device *dev) {
    const ll_pressure_sensor_data_t *data = dev->data;

    return data->raw_data;
}

/* Enables the given pressure sensor */
static int ll_pressure_sensor_enable(const struct device *dev) {
    const ll_pressure_sensor_cfg_t *cfg = dev->config;

    /* Trigger asynchronous ADC sampling */
    int ret = adc_read_async(cfg->adc_dev, &cfg->sequence, NULL);
    if (ret != 0) {
        LOG_ERR("Failed to start ADC continuous conversion with DMA (err %d)", ret);
    }

    return ret;
}

/* Convert frequency in hertz to period in microseconds */
#define FREQ_HZ_TO_PER_US(__freq__) (uint32_t)(1000000.0f / __freq__)

#define PRESSURE_SENSOR_INST(idx)                                                                                      \
    static ll_pressure_sensor_data_t pressure_sensor_data_##idx = {                                                    \
        .raw_data = 0,                                                                                                 \
    };                                                                                                                 \
    static const struct adc_sequence_options pressure_sensor_sequence_opts_##idx = {                                   \
        .extra_samplings = 0,                                                                                          \
        .callback = adc_cb,                                                                                            \
        .user_data = NULL,                                                                                             \
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
