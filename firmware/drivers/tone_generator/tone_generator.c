#include "tone_generator.h"

#include <stm32_ll_dac.h>
#include <stm32_ll_tim.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_stm32.h>
#include <zephyr/dt-bindings/dma/stm32_dma.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT ll_tone_generator

LOG_MODULE_REGISTER(ll_tone_generator, CONFIG_LL_TONE_GENERATOR_LOG_LEVEL);

/* 256-Sample 12-bit Sine Wave */
static const uint32_t sine_wave[256] = {
    0x800, 0x832, 0x864, 0x896, 0x8C8, 0x8FA, 0x92C, 0x95E, 0x98F, 0x9C0, 0x9F1, 0xA22, 0xA52, 0xA82, 0xAB1, 0xAE0,
    0xB0F, 0xB3D, 0xB6B, 0xB98, 0xBC5, 0xBF1, 0xC1C, 0xC47, 0xC71, 0xC9A, 0xCC3, 0xCEB, 0xD12, 0xD39, 0xD5F, 0xD83,
    0xDA7, 0xDCA, 0xDED, 0xE0E, 0xE2E, 0xE4E, 0xE6C, 0xE8A, 0xEA6, 0xEC1, 0xEDC, 0xEF5, 0xF0D, 0xF24, 0xF3A, 0xF4F,
    0xF63, 0xF76, 0xF87, 0xF98, 0xFA7, 0xFB5, 0xFC2, 0xFCD, 0xFD8, 0xFE1, 0xFE9, 0xFF0, 0xFF5, 0xFF9, 0xFFD, 0xFFE,
    0xFFF, 0xFFE, 0xFFD, 0xFF9, 0xFF5, 0xFF0, 0xFE9, 0xFE1, 0xFD8, 0xFCD, 0xFC2, 0xFB5, 0xFA7, 0xF98, 0xF87, 0xF76,
    0xF63, 0xF4F, 0xF3A, 0xF24, 0xF0D, 0xEF5, 0xEDC, 0xEC1, 0xEA6, 0xE8A, 0xE6C, 0xE4E, 0xE2E, 0xE0E, 0xDED, 0xDCA,
    0xDA7, 0xD83, 0xD5F, 0xD39, 0xD12, 0xCEB, 0xCC3, 0xC9A, 0xC71, 0xC47, 0xC1C, 0xBF1, 0xBC5, 0xB98, 0xB6B, 0xB3D,
    0xB0F, 0xAE0, 0xAB1, 0xA82, 0xA52, 0xA22, 0x9F1, 0x9C0, 0x98F, 0x95E, 0x92C, 0x8FA, 0x8C8, 0x896, 0x864, 0x832,
    0x800, 0x7CD, 0x79B, 0x769, 0x737, 0x705, 0x6D3, 0x6A1, 0x670, 0x63F, 0x60E, 0x5DD, 0x5AD, 0x57D, 0x54E, 0x51F,
    0x4F0, 0x4C2, 0x494, 0x467, 0x43A, 0x40E, 0x3E3, 0x3B8, 0x38E, 0x365, 0x33C, 0x314, 0x2ED, 0x2C6, 0x2A0, 0x27C,
    0x258, 0x235, 0x212, 0x1F1, 0x1D1, 0x1B1, 0x193, 0x175, 0x159, 0x13E, 0x123, 0x10A, 0x0F2, 0x0DB, 0x0C5, 0x0B0,
    0x09C, 0x089, 0x078, 0x067, 0x058, 0x04A, 0x03D, 0x032, 0x027, 0x01E, 0x016, 0x00F, 0x00A, 0x006, 0x002, 0x001,
    0x000, 0x001, 0x002, 0x006, 0x00A, 0x00F, 0x016, 0x01E, 0x027, 0x032, 0x03D, 0x04A, 0x058, 0x067, 0x078, 0x089,
    0x09C, 0x0B0, 0x0C5, 0x0DB, 0x0F2, 0x10A, 0x123, 0x13E, 0x159, 0x175, 0x193, 0x1B1, 0x1D1, 0x1F1, 0x212, 0x235,
    0x258, 0x27C, 0x2A0, 0x2C6, 0x2ED, 0x314, 0x33C, 0x365, 0x38E, 0x3B8, 0x3E3, 0x40E, 0x43A, 0x467, 0x494, 0x4C2,
    0x4F0, 0x51F, 0x54E, 0x57D, 0x5AD, 0x5DD, 0x60E, 0x63F, 0x670, 0x6A1, 0x6D3, 0x705, 0x737, 0x769, 0x79B, 0x7CD};

/* Macro to map Channel Number to LL DAC Channel Constant*/
#define DAC_CHANNEL_NUM_TO_LL_MAP(ch) ((ch) == 1 ? LL_DAC_CHANNEL_1 : LL_DAC_CHANNEL_2)

#define GET_LL_DAC_TIM_TRIGGER_SOURCE(__DACx__, __TIMx__)                                 \
    (((__DACx__) == DAC1 && (__TIMx__) == TIM2)             ? LL_DAC_TRIG_EXT_TIM2_TRGO   \
     : ((__DACx__) == DAC1 && (__TIMx__) == TIM3)           ? LL_DAC_TRIG_EXT_TIM3_TRGO   \
     : ((__DACx__) == DAC1 && (__TIMx__) == TIM4)           ? LL_DAC_TRIG_EXT_TIM4_TRGO   \
     : ((__DACx__) == DAC1 && (__TIMx__) == TIM6)           ? LL_DAC_TRIG_EXT_TIM6_TRGO   \
     : ((__DACx__) == DAC1 && (__TIMx__) == TIM7)           ? LL_DAC_TRIG_EXT_TIM7_TRGO   \
     : ((__DACx__) == DAC1 && (__TIMx__) == TIM15)          ? LL_DAC_TRIG_EXT_TIM15_TRGO  \
     : ((__DACx__) == DAC1 && (__TIMx__) == (void *)HRTIM1) ? LL_DAC_TRIG_EXT_HRTIM_TRGO1 \
     : ((__DACx__) == DAC2 && (__TIMx__) == TIM2)           ? LL_DAC_TRIG_EXT_TIM2_TRGO   \
     : ((__DACx__) == DAC2 && (__TIMx__) == TIM4)           ? LL_DAC_TRIG_EXT_TIM4_TRGO   \
     : ((__DACx__) == DAC2 && (__TIMx__) == TIM6)           ? LL_DAC_TRIG_EXT_TIM6_TRGO   \
     : ((__DACx__) == DAC2 && (__TIMx__) == TIM15)          ? LL_DAC_TRIG_EXT_TIM15_TRGO  \
     : ((__DACx__) == DAC2 && (__TIMx__) == (void *)HRTIM1) ? LL_DAC_TRIG_EXT_HRTIM_TRGO2 \
     : ((__DACx__) == DAC3 && (__TIMx__) == TIM1)           ? LL_DAC_TRIG_EXT_TIM1_TRGO   \
     : ((__DACx__) == DAC3 && (__TIMx__) == TIM8)           ? LL_DAC_TRIG_EXT_TIM8_TRGO   \
     : ((__DACx__) == DAC3 && (__TIMx__) == TIM2)           ? LL_DAC_TRIG_EXT_TIM2_TRGO   \
     : ((__DACx__) == DAC3 && (__TIMx__) == TIM3)           ? LL_DAC_TRIG_EXT_TIM3_TRGO   \
     : ((__DACx__) == DAC3 && (__TIMx__) == TIM4)           ? LL_DAC_TRIG_EXT_TIM4_TRGO   \
     : ((__DACx__) == DAC3 && (__TIMx__) == TIM6)           ? LL_DAC_TRIG_EXT_TIM6_TRGO   \
     : ((__DACx__) == DAC3 && (__TIMx__) == (void *)HRTIM1) ? LL_DAC_TRIG_EXT_HRTIM_TRGO3 \
     : ((__DACx__) == DAC4 && (__TIMx__) == (void *)HRTIM1) ? LL_DAC_TRIG_EXT_HRTIM_TRGO1 \
                                                            : -1)

/* Macro to acquire 12-bit right-aligned data holding register for the given DAC instance and channel */
#define GET_DACx_DHR12Rx_REGISTER(__DACx__, __CHANNELx__) \
    ((__CHANNELx__) == LL_DAC_CHANNEL_1 ? &(((DAC_TypeDef *)__DACx__)->DHR12R1) : &(((DAC_TypeDef *)__DACx__)->DHR12R2))

typedef struct {
    TIM_TypeDef *sample_rate_timer;
    DAC_TypeDef *dac;
    struct stm32_pclken tim_clk;
    struct stm32_pclken dac_clk;
    const struct device *dma_dev;
    uint32_t dac_channel;
    uint32_t dac_resolution;
    uint32_t dma_channel;
    uint32_t max_frequency;
} ll_tone_generator_cfg_t;

typedef struct {
    bool initialized;
    bool enabled;
    struct dma_config dma_cfg;
    struct k_timer duration_timer;
} ll_tone_generator_data_t;

/* Callback to handle DMA transfer error */
static void dma_callback(const struct device *dma_dev, void *user_data, uint32_t channel, int status) {
    if (status < 0) {
        LOG_ERR("DMA encountered error and entered interrupt with status: %d", status);
        ll_tone_generator_abort_tone((const struct device *)user_data);
    }
}

/*  Kernel Timer Expirey Callback function*/
static void duration_expiry_cb(struct k_timer *timer) {
    LOG_DBG("Duration Timer Expired");
    ll_tone_generator_abort_tone((const struct device *)timer->user_data);
}

/* Enable the provided clock */
static inline int ll_tone_generator_enable_clock(const struct stm32_pclken *clk) {
    const struct device *rcc = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
    if (!device_is_ready(rcc)) {
        return -ENODEV;
    }
    return clock_control_on(rcc, (clock_control_subsys_t *)clk);
}

/* DMA Initialization */
static int ll_tone_generator_dma_init(const struct device *dev) {
    const ll_tone_generator_cfg_t *cfg = dev->config;
    ll_tone_generator_data_t *data = dev->data;
    int ret;

    if (!cfg->dma_dev) {
        LOG_ERR("dma_dev is NULL");
        return -ENODEV;
    }

    ret = dma_config(cfg->dma_dev, cfg->dma_channel, &data->dma_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to configure DMA: %d", ret);
        return ret;
    }

    return ret;
}

/* TIM Initialization */
static int ll_tone_generator_tim_init(const struct device *dev) {
    const ll_tone_generator_cfg_t *cfg = dev->config;
    int ret;

    ret = ll_tone_generator_enable_clock(&cfg->tim_clk);
    if (ret != 0) {
        LOG_ERR("Failed to enable sample rate timer clock: %d", ret);
        return ret;
    }

    /* Declare TIM initialization structure */
    LL_TIM_InitTypeDef tim_init = {0};

    /* Initialize the TIM initializationn structure */
    LL_TIM_StructInit(&tim_init);

    /*
    Populate the TIM initialization structure.
    Sticking with smallest prescaler to allow for maximum resolution
    */
    tim_init.Prescaler = 0x0;
    tim_init.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim_init.Autoreload = 0x0001;
    tim_init.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;

    /* Enable TIM Clock */
    ret = LL_TIM_Init(cfg->sample_rate_timer, &tim_init);
    if (ret != 0) {
        LOG_ERR("Failed to initialize TIM: %d", ret);
        return ret;
    }

    /* Enable Auto-Reload Preload (Buffer the auto-reload register) */
    LL_TIM_EnableARRPreload(cfg->sample_rate_timer);

    /* Set trigger to output on UPDATE event */
    LL_TIM_SetTriggerOutput(cfg->sample_rate_timer, LL_TIM_TRGO_UPDATE);

    /* Disable Master/Slave mode */
    LL_TIM_DisableMasterSlaveMode(cfg->sample_rate_timer);

    return ret;
}

/* DAC Initialization */
static int ll_tone_generator_dac_init(const struct device *dev) {
    const ll_tone_generator_cfg_t *cfg = dev->config;
    int ret;

    /* Enable DAC Clock */
    ret = ll_tone_generator_enable_clock(&cfg->dac_clk);
    if (ret != 0) {
        LOG_ERR("Failed to enable DAC clock: %d", ret);
        return ret;
    }

    /* Declare DAC initialization structure */
    LL_DAC_InitTypeDef dac_init = {0};

    /* Initialize the DAC initializationn structure */
    LL_DAC_StructInit(&dac_init);

    /* Populate the DAC initialization structure. */
    dac_init.TriggerSource = GET_LL_DAC_TIM_TRIGGER_SOURCE(
        cfg->dac, cfg->sample_rate_timer);  // Determine trigger source from chosen DAC and timer

    dac_init.TriggerSource2 =
        LL_DAC_TRIG_SOFTWARE;  // Set secondary trigger source to software to allow for clearing of output
    dac_init.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE;  // No auto generation of waveform occuring
    dac_init.OutputBuffer = LL_DAC_OUTPUT_BUFFER_ENABLE;     // Buffer output to prevent glitches in output waveform
    dac_init.OutputConnection = LL_DAC_OUTPUT_CONNECT_GPIO;  // Connect DAC output to GPIO
    dac_init.OutputMode = LL_DAC_OUTPUT_MODE_NORMAL;         // Output mode is NORMAL

    ret = LL_DAC_Init(cfg->dac, cfg->dac_channel, &dac_init);
    if (ret != 0) {
        LOG_ERR("Failed to Initialize DAC: %d", ret);
        return ret;
    }

    return ret;
}

/* Tone Generator Initialization Function */
static int ll_tone_generator_init(const struct device *dev) {
    ll_tone_generator_data_t *data = dev->data;
    int ret;

    LOG_INF("Initializing Tone Generator...");

    /* Initialize the duration timer */
    k_timer_init(&data->duration_timer, duration_expiry_cb, NULL);

    /* Place tone generator in dma_cfg user_data so it can be used to handle errors appropriately */
    data->dma_cfg.user_data = (void *)dev;

    /* Place tone generator in k_timer user_data so it can be passed to ll_tone_generator_abort_tone() */
    data->duration_timer.user_data = (void *)dev;

    /* Initialize the sample rate timer TIM peripheral */
    ret = ll_tone_generator_tim_init(dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize sample rate timer: %d", ret);
        return ret;
    }

    /* Initialize the DAC peripheral */
    ret = ll_tone_generator_dac_init(dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize DAC: %d", ret);
        return ret;
    }

    /* Intialize the DMA peripheral */
    ret = ll_tone_generator_dma_init(dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize DMA: %d", ret);
        return ret;
    }

    LOG_INF("Tone Generator Initialized!");
    data->initialized = true;
    return ret;
}

/**
 * Generates a tone with the given frequency and duration on the provided tone generator.
 */
int ll_tone_generator_play_tone(const struct device *dev, unsigned int frequency_hz, unsigned int duration_ms) {
    const ll_tone_generator_cfg_t *cfg = dev->config;
    ll_tone_generator_data_t *data = dev->data;
    int ret;

    /* Ensure that the tone generator has been initialized */
    if (!data->initialized) {
        LOG_ERR("Tone Generator must be initialized before play tone can be called");
        return -1;
    }

    /* If a tone is actively being played, stop it before continuing */
    if (data->enabled) {
        ll_tone_generator_abort_tone(dev);
    }

    /* Start the DMA transfer */
    ret = dma_start(cfg->dma_dev, cfg->dma_channel);
    if (ret != 0) {
        LOG_ERR("Failed to start DMA: %d", ret);
        return ret;
    }

    /* Calculate the period corresponding to the given frequency, and write to sample rate timer */
    LL_TIM_SetAutoReload(
        cfg->sample_rate_timer,
        __LL_TIM_CALC_ARR(SystemCoreClock, 0x0, frequency_hz * (sizeof(sine_wave) / sizeof(sine_wave[0]))));

    /* Enable counter */
    LL_TIM_EnableCounter(cfg->sample_rate_timer);

    /* Enable DAC Trigger */
    LL_DAC_EnableTrigger(cfg->dac, cfg->dac_channel);

    /* Enable DAC channel DMA request */
    LL_DAC_EnableDMAReq(cfg->dac, cfg->dac_channel);

    /* Enable DAC */
    LL_DAC_Enable(cfg->dac, cfg->dac_channel);

    /* Start the duration timer to expire after the given duration in milliseconds in one-shot mode */
    k_timer_start(&data->duration_timer, K_MSEC(duration_ms), K_NO_WAIT);

    /* Set the enabled flag to true */
    data->enabled = true;

    LOG_INF("Playing tone at %uHz for %ums", frequency_hz, duration_ms);

    return ret;
}

/**
 * Generates a tone with the given frequency and duration on the provided
 * tone generator. Blocks for the duration of the tone.
 */
int ll_tone_generator_play_tone_blocking(const struct device *dev, unsigned int frequency_hz,
                                         unsigned int duration_ms) {
    int ret;

    /* Start playing tone */
    ret = ll_tone_generator_play_tone(dev, frequency_hz, duration_ms);

    /* Sleep for duration of tone */
    k_msleep(duration_ms);

    return ret;
}

/* Halts the tone generation proccess on the given tone generator */
int ll_tone_generator_abort_tone(const struct device *dev) {
    const ll_tone_generator_cfg_t *cfg = dev->config;
    ll_tone_generator_data_t *data = dev->data;
    int ret;

    if (!data->initialized) {
        LOG_ERR("Tone Generator must be initialized before abort tone can be called");
    }

    /* Ensure that the duration time has been stopped - does nothing if already expired */
    k_timer_stop(&data->duration_timer);

    /* Stop the DMA transfer */
    ret = dma_stop(cfg->dma_dev, cfg->dma_channel);
    if (ret != 0) {
        LOG_ERR("Failed to stop DMA: %d", ret);
        return ret;
    }

    /* Disable the sample rate timer */
    LL_TIM_DisableCounter(cfg->sample_rate_timer);

    /* Disable the DAC */
    LL_DAC_Disable(cfg->dac, cfg->dac_channel);
    data->enabled = false;

    return ret;
}

#define TONE_GENERATOR_INST(idx)                                                                                    \
    static struct dma_block_config blk_cfg_##idx = {                                                                \
        .block_size = sizeof(sine_wave),                                                                            \
        .source_address = (uint32_t)sine_wave,                                                                      \
        .dest_address = (uint32_t)GET_DACx_DHR12Rx_REGISTER(                                                        \
            DT_REG_ADDR(DT_INST_PROP(idx, dac)), DAC_CHANNEL_NUM_TO_LL_MAP(DT_INST_PROP(idx, dac_channel))),        \
        .source_addr_adj = DMA_ADDR_ADJ_INCREMENT,                                                                  \
        .dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,                                                                    \
        .source_reload_en = true,                                                                                   \
        .dest_reload_en = true,                                                                                     \
    };                                                                                                              \
                                                                                                                    \
    static const ll_tone_generator_cfg_t tone_generator_cfg_##idx = {                                               \
        .sample_rate_timer = (TIM_TypeDef *)DT_REG_ADDR(DT_INST_PROP(idx, sample_rate_timer)),                      \
        .tim_clk = (struct stm32_pclken){.bus = DT_CLOCKS_CELL(DT_INST_PROP(idx, sample_rate_timer), bus),          \
                                         .enr = DT_CLOCKS_CELL(DT_INST_PROP(idx, sample_rate_timer), bits)},        \
        .dac = (DAC_TypeDef *)DT_REG_ADDR(DT_INST_PROP(idx, dac)),                                                  \
        .dac_clk = (struct stm32_pclken){.bus = DT_CLOCKS_CELL(DT_INST_PROP(idx, dac), bus),                        \
                                         .enr = DT_CLOCKS_CELL(DT_INST_PROP(idx, dac), bits)},                      \
        .dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR(idx)),                                                           \
        .dac_channel = DAC_CHANNEL_NUM_TO_LL_MAP(DT_INST_PROP(idx, dac_channel)),                                   \
        .dma_channel = DT_INST_DMAS_CELL_BY_NAME(idx, tx, channel),                                                 \
    };                                                                                                              \
                                                                                                                    \
    static ll_tone_generator_data_t tone_generator_data_##idx = {                                                   \
        .initialized = false,                                                                                       \
        .enabled = false,                                                                                           \
        .dma_cfg =                                                                                                  \
            {                                                                                                       \
                .block_count = 1,                                                                                   \
                .dma_slot = STM32_DMA_SLOT(idx, tx, slot),                                                          \
                .channel_direction = STM32_DMA_CONFIG_DIRECTION(STM32_DMA_MEMORY_TO_PERIPH),                        \
                .source_data_size = 4,                                                                              \
                .dest_data_size = 4,                                                                                \
                .source_burst_length = 1,                                                                           \
                .dest_burst_length = 1,                                                                             \
                .channel_priority = STM32_DMA_CONFIG_PRIORITY(STM32_DMA_CHANNEL_CONFIG(idx, tx)),                   \
                .head_block = &blk_cfg_##idx,                                                                       \
                .complete_callback_en = false,                                                                      \
                .error_callback_dis = true,                                                                         \
                .dma_callback = dma_callback,                                                                       \
                .user_data = NULL,                                                                                  \
                .cyclic = true,                                                                                     \
            },                                                                                                      \
    };                                                                                                              \
                                                                                                                    \
    DEVICE_DT_INST_DEFINE(idx, ll_tone_generator_init, NULL, &tone_generator_data_##idx, &tone_generator_cfg_##idx, \
                          POST_KERNEL, CONFIG_LL_TONE_GENERATOR_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TONE_GENERATOR_INST)
