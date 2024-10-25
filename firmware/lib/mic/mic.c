
#include "mic.h"

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define NODE_NAME i2s_mic

LOG_MODULE_REGISTER(NODE_NAME, CONFIG_LIB_MIC_LOG_LEVEL);

#if !DT_NODE_EXISTS(DT_ALIAS(NODE_NAME))
#error "MIC Alias is not defined in the DTS"
#endif

#define MIC_INITIALIZED 0xDEADC0DE
#define MIC_ENABLED 0xC0EDC0DE

#define SAMPLE_FREQUENCY 22000  // 44100
#define INTEGRAL_TYPE int32_t
#define BYTES_PER_SAMPLE sizeof(INTEGRAL_TYPE)
#define SAMPLE_BIT_WIDTH (BYTES_PER_SAMPLE * CHAR_BIT)
#define NUMBER_OF_CHANNELS 2
/* Such block length provides an echo with the delay of 100 ms. TODO - Power of 2 */
#define SAMPLES_PER_BLOCK ((SAMPLE_FREQUENCY / 10) * NUMBER_OF_CHANNELS)
#define TIMEOUT_MSEC 1000

#define BLOCK_SIZE (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT 3  // one active, one processing, one on-deck

K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, BYTES_PER_SAMPLE);

/* -------------------------------------------------------------------------- */

bool mic_initialize(Microphone* mic) {
    const struct i2s_config config = {.word_size = SAMPLE_BIT_WIDTH,
                                      .channels = NUMBER_OF_CHANNELS,
                                      .format = I2S_FMT_DATA_FORMAT_I2S,
                                      .options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
                                      .frame_clk_freq = SAMPLE_FREQUENCY,
                                      .mem_slab = &mem_slab,
                                      .block_size = BLOCK_SIZE,
                                      .timeout = TIMEOUT_MSEC};

    // Set initial conditions for structure
    mic->streamEnabled = ~MIC_ENABLED;
    mic->initialized = ~MIC_INITIALIZED;
    const struct device* device = (const struct device*)DEVICE_DT_GET(DT_ALIAS(NODE_NAME));

    // verify device exists and is ready to be used

    if (!device) {
        LOG_ERR("Device i2s_mic not found in device tree.\n");
        return false;
    }

    if (!device_is_ready(device)) {
        LOG_ERR("%s is not ready\n", device->name);
        return false;
    }

    // Configure the device

    const int ret = i2s_configure(device, I2S_DIR_RX, &config);
    if (ret < 0) {
        LOG_ERR("Failed to configure MIC input stream: %d\n", ret);
        return false;
    }

    mic->device = device;
    mic->initialized = MIC_INITIALIZED;

    return true;
}

/* -------------------------------------------------------------------------- */

bool mic_enable_reads(Microphone* mic, const bool enable) {
    if (MIC_INITIALIZED != mic->initialized) {
        LOG_ERR("Must initialize the microphone entity before calling mic_enable_reads\n");
        return false;
    }

    const int ret = i2s_trigger(mic->device, I2S_DIR_RX, enable ? I2S_TRIGGER_START : I2S_TRIGGER_DROP);

    if (ret < 0) {
        LOG_ERR("Failed to %sable streaming: %d\n", enable ? "en" : "dis", ret);
        mic->streamEnabled = ~MIC_ENABLED;
        return false;
    }

    mic->streamEnabled = enable ? MIC_ENABLED : ~MIC_ENABLED;

    return true;
}

/* -------------------------------------------------------------------------- */

bool mic_read(const Microphone* mic, void** mem_block, uint32_t* block_size) {
    // Make sure user doesn't use old or invalid data
    mem_block = NULL;
    block_size = 0;

    if (MIC_INITIALIZED != mic->initialized) {
        LOG_ERR("Must initialize the microphone entity before calling mic_read\n");
        return false;
    }

    if (MIC_ENABLED != mic->streamEnabled) {
        LOG_ERR("Must enable reads before calling mic_read\n");
        return false;
    }

    const int ret = i2s_read(mic->device, mem_block, block_size);
    if (ret < 0) {
        LOG_ERR("Failed to read data: %d\n", ret);
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------- */

void mic_release_buffer(void* mem_block) { k_mem_slab_free(&mem_slab, mem_block); }