#include "analog_out.h"

#include <errno.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_analog_out

LOG_MODULE_REGISTER(ll_analog_out, CONFIG_LL_ANALOG_OUT_LOG_LEVEL);

#define MV_TO_DAC_COUNTS(__mv__, __max_dac_counts__) (uint16_t)((__mv__ * __max_dac_counts__) / ANALOG_OUT_MAX_VALUE_MV)

typedef struct {
    const struct device *dac_dev;
    struct dac_channel_cfg dac_ch_cfg;
    uint16_t max_adc_counts;
} ll_analog_out_cfg_t;

typedef struct {
    bool initialized;
    uint16_t value_mv;
} ll_analog_out_data_t;

/* Writes the specified value to the given analog output instance */
static ll_analog_out_error_t ll_analog_out_write_value(const struct device *dev, uint16_t value) {
    const ll_analog_out_cfg_t *cfg = dev->config;
    ll_analog_out_data_t *data = dev->data;
    int ret;

    if (!data->initialized) {
        LOG_ERR("Failed to write analog output value: %s", analog_out_error_to_str[ANALOG_OUT_NOT_INITIALIZED]);
        return ANALOG_OUT_NOT_INITIALIZED;
    }

    if (value < 0 || value > cfg->max_adc_counts) {
        LOG_ERR("Failed to write analog output value: %s - %d", analog_out_error_to_str[ANALOG_OUT_INVALID_VALUE],
                value);
        return ANALOG_OUT_INVALID_VALUE;
    }

    ret = dac_write_value(cfg->dac_dev, cfg->dac_ch_cfg.channel_id, value);
    if (ret != 0) {
        LOG_ERR("Failed to write analog output value: %s - %d", analog_out_error_to_str[ANALOG_OUT_INVALID_CHANNEL],
                cfg->dac_ch_cfg.channel_id);
        return ANALOG_OUT_INVALID_CHANNEL;
    }

    return ANALOG_OUT_NO_ERROR;
}

/* Writes the specified value in mV to the given analog output instance */
ll_analog_out_error_t ll_analog_out_write_value_mv(const struct device *dev, int value_mv) {
    const ll_analog_out_cfg_t *cfg = dev->config;
    ll_analog_out_data_t *data = dev->data;

    if (value_mv < 0 || value_mv > ANALOG_OUT_MAX_VALUE_MV) {
        LOG_ERR("Failed to write analog output value: %s - %dmV - must be in range [0.0, %1.1f]",
                analog_out_error_to_str[ANALOG_OUT_INVALID_VALUE], value_mv, ANALOG_OUT_MAX_VALUE_MV);
        return ANALOG_OUT_INVALID_VALUE;
    }

    ll_analog_out_error_t ret = ll_analog_out_write_value(dev, MV_TO_DAC_COUNTS(value_mv, cfg->max_adc_counts));
    if (ret == ANALOG_OUT_NO_ERROR) {
        data->value_mv = value_mv;
    }

    return ret;
}

/* Returns the current analog out output value in mv */
uint16_t ll_analog_out_get_value_mv(const struct device *dev) {
    ll_analog_out_data_t *data = dev->data;

    return data->value_mv;
}

/* Initialize the given analog output instance */
static int ll_analog_out_init(const struct device *dev) {
    const ll_analog_out_cfg_t *cfg = dev->config;
    ll_analog_out_data_t *data = dev->data;
    int ret;

    LOG_INF("Initializing Analog Out...");

    ret = dac_channel_setup(cfg->dac_dev, &cfg->dac_ch_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to initialize Analog Out: Setup of DAC channel failed with code %d\n", ret);
        return ret;
    }

    data->initialized = true;
    LOG_INF("Analog Out initialized!");

    return ret;
}

#define ANALOG_OUT_INST(idx)                                                                                         \
    static const ll_analog_out_cfg_t analog_out_cfg_##idx = {                                                        \
        .dac_dev = DEVICE_DT_GET(DT_INST_IO_CHANNELS_CTLR(idx)),                                                     \
        .dac_ch_cfg =                                                                                                \
            {                                                                                                        \
                .channel_id = DT_INST_PHA_BY_IDX(idx, io_channels, 0, output),                                       \
                .resolution = DT_INST_PROP(idx, resolution),                                                         \
                .buffered = true,                                                                                    \
            },                                                                                                       \
        .max_adc_counts = (1 << DT_INST_PROP(idx, resolution)) - 1,                                                  \
    };                                                                                                               \
                                                                                                                     \
    static ll_analog_out_data_t analog_out_data_##idx = {                                                            \
        .initialized = false,                                                                                        \
        .value_mv = 0,                                                                                               \
    };                                                                                                               \
                                                                                                                     \
    DEVICE_DT_INST_DEFINE(idx, ll_analog_out_init, NULL, &analog_out_data_##idx, &analog_out_cfg_##idx, POST_KERNEL, \
                          CONFIG_LL_ANALOG_OUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ANALOG_OUT_INST)
