#include "motor_common.h"


#include <zephyr/logging/log.h>
#include <zephyr/drivers/pinctrl.h>


LOG_MODULE_REGISTER(ll_motor, CONFIG_LL_MOTOR_LOG_LEVEL);

void ll_motor_dma_tx_callback(const struct device *dma_dev, void *arg, uint32_t channel, int status) {
    const struct device *dev = arg;
    const ll_motor_cfg_t *cfg = dev->config;
    ll_motor_data_t *data = dev->data;

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
    } else {
        // If there are no more blocks, stop the timer
        // FIXME: Do we want to continue to hold the last position? Or just stop sending pulses?
        LL_TIM_DisableCounter(cfg->timer);
    }
}

int ll_motor_start_dma(const struct device *dev) {
    const ll_motor_cfg_t *cfg = dev->config;
    ll_motor_data_t *data = dev->data;

    // Grab the initial block of positions from the msgq
    ll_motorq_msg_t msg;
    if (k_msgq_get(cfg->msgq, &msg, K_NO_WAIT) < 0) {
        LOG_ERR("Failed to get initial position block");
        return -EIO;
    }

    struct dma_block_config blk_cfg;

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
    LL_TIM_ConfigDMABurst(cfg->timer, cfg->timer_dma_reg, LL_TIM_DMABURST_LENGTH_1TRANSFER);
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

int ll_motor_queue_data(const struct device *dev, uint32_t *buf, size_t len, k_timeout_t timeout) {
    const ll_motor_cfg_t *cfg = dev->config;
    ll_motor_data_t *data = dev->data;

    ll_motorq_msg_t msg = {
        .buf = buf,
        .buf_size = len,
    };

    // Queue the memory block for sending
    int ret = k_msgq_put(cfg->msgq, &msg, timeout);

    // If this was the first block and the DMA is idle, start the DMA transfer
    struct dma_status status;
    dma_get_status(cfg->dma_dev, data->dma_channel, &status);
    if (k_msgq_num_used_get(cfg->msgq) == 1 && status.busy == false) {
        ret = ll_motor_start_dma(dev);
    }

    return ret;
}

int ll_motor_init(const struct device *dev) {
    const ll_motor_cfg_t *cfg = dev->config;

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

    return 0;
}
