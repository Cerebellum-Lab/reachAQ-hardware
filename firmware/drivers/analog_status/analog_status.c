#include "analog_status.h"

#include <errno.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_analog_status

LOG_MODULE_REGISTER(ll_analog_status, CONFIG_LL_ANALOG_STATUS_LOG_LEVEL);

#define MV_TO_DAC_COUNTS(__mv__, __max_dac_counts__) \
    (uint16_t)((__mv__ * __max_dac_counts__) / ANALOG_STATUS_MAX_VALUE_MV)

typedef struct {
    const struct device *dac_dev;
    struct dac_channel_cfg dac_ch_cfg;
    uint16_t max_adc_counts;
} ll_analog_status_cfg_t;

typedef struct {
    bool initialized;
} ll_analog_status_data_t;

/* Writes the specified value to the given analog status instance */
static ll_analog_status_error_t ll_analog_status_write_value(const struct device *dev, uint16_t value) {
    const ll_analog_status_cfg_t *cfg = dev->config;
    ll_analog_status_data_t *data = dev->data;
    int ret;

    if (!data->initialized) {
        LOG_ERR("Failed to write analog status value: %s", analog_status_error_to_str[ANALOG_STATUS_NOT_INITIALIZED]);
        return ANALOG_STATUS_NOT_INITIALIZED;
    }

    if (value < 0 || value > cfg->max_adc_counts) {
        LOG_ERR("Failed to write analog status value: %s - %d", analog_status_error_to_str[ANALOG_STATUS_INVALID_VALUE],
                value);
        return ANALOG_STATUS_INVALID_VALUE;
    }

    ret = dac_write_value(cfg->dac_dev, cfg->dac_ch_cfg.channel_id, value);
    if (ret != 0) {
        LOG_ERR("Failed to write analog status value: %s - %d",
                analog_status_error_to_str[ANALOG_STATUS_INVALID_CHANNEL], cfg->dac_ch_cfg.channel_id);
        return ANALOG_STATUS_INVALID_CHANNEL;
    }

    return ANALOG_STATUS_NO_ERROR;
}

/* Writes the specified value in mV to the given analog status instance */
ll_analog_status_error_t ll_analog_status_write_value_mv(const struct device *dev, int value_mv) {
    const ll_analog_status_cfg_t *cfg = dev->config;

    if (value_mv < 0 || value_mv > ANALOG_STATUS_MAX_VALUE_MV) {
        LOG_ERR("Failed to write analog status value: %s - %dmV - must be in range [0.0, %1.1f]",
                analog_status_error_to_str[ANALOG_STATUS_INVALID_VALUE], value_mv, ANALOG_STATUS_MAX_VALUE_MV);
        return ANALOG_STATUS_INVALID_VALUE;
    }

    ll_analog_status_error_t ret = ll_analog_status_write_value(dev, MV_TO_DAC_COUNTS(value_mv, cfg->max_adc_counts));
    return ret;
}

/* Initialize the given analog status instance */
static int ll_analog_status_init(const struct device *dev) {
    const ll_analog_status_cfg_t *cfg = dev->config;
    ll_analog_status_data_t *data = dev->data;
    int ret;

    ret = dac_channel_setup(cfg->dac_dev, &cfg->dac_ch_cfg);
    if (ret != 0) {
        LOG_ERR("Setting up of DAC channel failed with code %d\n", ret);
        return ret;
    }

    data->initialized = true;
    return ret;
}

#define ANALOG_STATUS_INST(idx)                                                                                  \
    static const ll_analog_status_cfg_t analog_status_cfg_##idx = {                                              \
        .dac_dev = DEVICE_DT_GET(DT_INST_IO_CHANNELS_CTLR(idx)),                                                 \
        .dac_ch_cfg =                                                                                            \
            {                                                                                                    \
                .channel_id = DT_INST_PHA_BY_IDX(idx, io_channels, 0, output),                                   \
                .resolution = DT_INST_PROP(idx, resolution),                                                     \
                .buffered = true,                                                                                \
            },                                                                                                   \
        .max_adc_counts = (1 << DT_INST_PROP(idx, resolution)) - 1,                                              \
    };                                                                                                           \
                                                                                                                 \
    static ll_analog_status_data_t analog_status_data_##idx = {                                                  \
        .initialized = false,                                                                                    \
    };                                                                                                           \
                                                                                                                 \
    DEVICE_DT_INST_DEFINE(idx, ll_analog_status_init, NULL, &analog_status_data_##idx, &analog_status_cfg_##idx, \
                          POST_KERNEL, CONFIG_LL_ANALOG_STATUS_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ANALOG_STATUS_INST)
