#include "stepper.h"

#include <stm32_ll_tim.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_stm32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/dma/stm32_dma.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "motor_common.h"

#define DT_DRV_COMPAT ll_stepper

LOG_MODULE_REGISTER(stepper, CONFIG_LL_STEPPER_LOG_LEVEL);

typedef struct {
    int32_t position;
    struct dma_config dma_cfg;
    int dma_channel;
} ll_stepper_data_t;

typedef struct {
    TIM_TypeDef *timer;
    const struct pinctrl_dev_config *pcfg;
    struct stm32_pclken clk;
    uint32_t prescaler;
    uint32_t channel;
    const struct device *dma_dev;
    struct k_msgq *msgq;
} ll_stepper_cfg_t;

static int ll_stepper_start_dma(const struct device *dev);

static void dma_tx_callback(const struct device *dma_dev, void *arg, uint32_t channel, int status) {
    const struct device *dev = arg;
    const ll_stepper_cfg_t *cfg = dev->config;
    ll_stepper_data_t *data = dev->data;

    // Check the msgq to see if there are any more position blocks to send
    ll_motorq_msg_t msg;
    if (k_msgq_get(cfg->msgq, &msg, K_NO_WAIT) == 0) {
        // Set up the next block
        int ret =
            dma_reload(cfg->dma_dev, data->dma_channel, (uint32_t)msg.buf, (uint32_t)&cfg->timer->DMAR, msg.buf_size);

        if (ret < 0) {
            LOG_ERR("Failed to reload DMA: %d", ret);
            return;
        }
    }
}

int ll_queue_stepper_positions(const struct device *dev, uint32_t *positions, size_t len, k_timeout_t timeout) {
    const ll_stepper_cfg_t *cfg = dev->config;
    ll_stepper_data_t *data = dev->data;

    ll_motorq_msg_t msg = {
        .buf = positions,
        .buf_size = len,
    };

    // Queue the memory block for sending
    int ret = k_msgq_put(cfg->msgq, &msg, timeout);

    // If this was the first block and the DMA is idle, start the DMA transfer
    struct dma_status status;
    dma_get_status(cfg->dma_dev, data->dma_channel, &status);
    if (k_msgq_num_used_get(cfg->msgq) == 1 && status.busy == false) {
        ret = ll_stepper_start_dma(dev);
    }

    return ret;
}

static int ll_stepper_start_dma(const struct device *dev) {
    const ll_stepper_cfg_t *cfg = dev->config;
    ll_stepper_data_t *data = dev->data;

    // Grab the initial block of positions from the msgq
    ll_motorq_msg_t msg;
    if (k_msgq_get(cfg->msgq, &msg, K_NO_WAIT) < 0) {
        LOG_ERR("Failed to get initial position block");
        return -EIO;
    }

    static struct dma_block_config blk_cfg;

    memset(&blk_cfg, 0, sizeof(blk_cfg));
    blk_cfg.block_size = msg.buf_size;
    blk_cfg.source_address = (uint32_t)msg.buf;
    blk_cfg.dest_address = (uint32_t)&cfg->timer->DMAR;
    blk_cfg.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

    data->dma_cfg.head_block = &blk_cfg;
    data->dma_cfg.user_data = (void *)dev;

    int ret = dma_config(cfg->dma_dev, data->dma_channel, &data->dma_cfg);
    if (ret < 0) {
        LOG_ERR("Failed to configure DMA: %d", ret);
        return ret;
    }

    // Set up the Timer to make DMA requests
    LL_TIM_ConfigDMABurst(cfg->timer, LL_TIM_DMABURST_BASEADDR_ARR, LL_TIM_DMABURST_LENGTH_1TRANSFER);
    LL_TIM_EnableDMAReq_UPDATE(cfg->timer);

    // Start the DMA transfer
    ret = dma_start(cfg->dma_dev, data->dma_channel);
    if (ret < 0) {
        LOG_ERR("Failed to start DMA: %d", ret);
        return ret;
    }

    // Generate an update to fetch the first value via DMA
    LL_TIM_GenerateEvent_UPDATE(cfg->timer);

    // Enable the counter
    LL_TIM_EnableCounter(cfg->timer);

    return 0;
}

static int ll_stepper_init(const struct device *dev) {
    const ll_stepper_cfg_t *cfg = dev->config;
    ll_stepper_data_t *data = dev->data;

    int ret = ll_motor_timer_enable_clock(&cfg->clk);
    if (ret < 0) {
        LOG_ERR("Failed to enable timer clock: %d", ret);
        return ret;
    }

    // Set up the output pin mux
    ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
    if (ret < 0) {
        LOG_ERR("Failed to apply pin state: %d", ret);
        return ret;
    }

    // Initialize the timer
    LL_TIM_InitTypeDef tim_init;
    LL_TIM_StructInit(&tim_init);
    tim_init.Prescaler = cfg->prescaler;
    tim_init.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim_init.Autoreload = 4000;
    tim_init.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;


    if (LL_TIM_Init(cfg->timer, &tim_init) != SUCCESS) {
        LOG_ERR("Failed to initialize timer");
        return -EIO;
    }

    LL_TIM_BDTR_InitTypeDef bdtr_init;
    LL_TIM_BDTR_StructInit(&bdtr_init);
    bdtr_init.BreakPolarity = LL_TIM_BREAK_POLARITY_HIGH;
    bdtr_init.Break2Polarity = LL_TIM_BREAK2_POLARITY_HIGH;
    bdtr_init.AutomaticOutput = LL_TIM_AUTOMATICOUTPUT_ENABLE;
    LL_TIM_BDTR_Init(cfg->timer, &bdtr_init);

    // Set up the output compare and pulse generator
    LL_TIM_OC_InitTypeDef output_chan_init;
    LL_TIM_OC_StructInit(&output_chan_init);
    output_chan_init.OCMode = LL_TIM_OCMODE_PULSE_ON_COMPARE;
    output_chan_init.CompareValue = 100;

    if (LL_TIM_OC_Init(cfg->timer, cfg->channel, &output_chan_init) != SUCCESS) {
        LOG_ERR("Failed to initialize output channel");
        return -EIO;
    }

    LL_TIM_OC_SetPulseWidth(cfg->timer, 170); // 1us - Pulse width is based on the kernel clock...

    // Enable the channel
    LL_TIM_CC_EnableChannel(cfg->timer, cfg->channel);

    return 0;
}

#define STEPPER_INST(idx)                                                                                 \
    PINCTRL_DT_INST_DEFINE(idx);                                                                          \
                                                                                                          \
    K_MSGQ_DEFINE(stepper_msgq##idx, sizeof(ll_motorq_msg_t), 16, 4);                                     \
                                                                                                          \
    static const ll_stepper_cfg_t stepper_cfg##idx = {                                                    \
        .timer = TIM(idx),                                                                                \
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),                                                      \
        .clk = DT_INST_CLK(idx, timer),                                                                   \
        .prescaler = DT_PROP(TIMER(idx), st_prescaler),                                                   \
        .channel = channel_to_ll_map[DT_INST_PROP(idx, pwm_channel) - 1],                                 \
        .dma_dev = DEVICE_DT_GET(STM32_DMA_CTLR(idx, tx)),                                                \
        .msgq = &stepper_msgq##idx,                                                                       \
    };                                                                                                    \
                                                                                                          \
    static ll_stepper_data_t stepper_data##idx = {                                                        \
        .position = 0,                                                                                    \
        .dma_channel = DT_INST_DMAS_CELL_BY_NAME(idx, tx, channel),                                       \
        .dma_cfg =                                                                                        \
            {                                                                                             \
                .block_count = 2,                                                                         \
                .dma_slot = STM32_DMA_SLOT(idx, tx, slot),                                                \
                .channel_direction = STM32_DMA_CONFIG_DIRECTION(STM32_DMA_MEMORY_TO_PERIPH),              \
                .source_data_size = 4,                                                                    \
                .dest_data_size = 4,                                                                      \
                .source_burst_length = 1,                                                                 \
                .dest_burst_length = 1,                                                                   \
                .channel_priority = STM32_DMA_CONFIG_PRIORITY(STM32_DMA_CHANNEL_CONFIG(idx, tx)),         \
                .dma_callback = dma_tx_callback,                                                          \
            },                                                                                            \
    };                                                                                                    \
                                                                                                          \
    DEVICE_DT_INST_DEFINE(idx, ll_stepper_init, NULL, &stepper_data##idx, &stepper_cfg##idx, POST_KERNEL, \
                          CONFIG_LL_STEPPER_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(STEPPER_INST)
