#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Microphone data set
 */
typedef struct _mic {
    uint32_t initialized;    // flag: init/not init
    uint32_t streamEnabled;  // flag: stream enabled/disabled
    const void* device;      // Handle to device
} Microphone;

/**
 * Initialize the microphone, configuring the device to receive data on the I2S
 * bus.
 *
 * @param - Pointer to uninitialized Microphone instance.
 */
[[nodiscard]] bool mic_initialize(Microphone* mic);

/**
 * Enable/Disable reading on the I2S bus.
 *
 * @param mic - Initialized Microphone
 * @param enable
 */
[[nodiscard]] bool mic_enable_reads(Microphone* mic, bool enable);

/**
 * Read a data set from the microphone. On success, mem_block and block_size
 * are updated with linkage to the read data.
 *
 * After any processing is completed, app MUST CALL mic_release_buffer on the
 * return memory block.
 *
 * @param[out] mem_block - Non-NULL pointer, will contain pointer to read data
 * @param[out] block_size - Non-NULL pointer, will contain the number of bytes read
 */
[[nodiscard]] bool mic_read(const Microphone* mic, void** mem_block, uint32_t* block_size);

/**
 * Releases the buffer to allow the microphone system to re-use the block.
 *
 * WARNING: if buffers are not released, no more data will become available.
 */
void mic_release_buffer(void* mem_block);
