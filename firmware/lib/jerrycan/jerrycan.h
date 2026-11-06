#pragma once

#include <zephyr/kernel.h>

#include "jerrycan_types.h"

// Run the main service loop for JerryCAN - RX callbacks will be processed in the context of the calling thread
// Returns 0 on success, -EAGAIN if no message is available, or another negative error code on failure
int jerrycan_run(k_timeout_t timeout);

// Register a callback to be called when a message of the specified type is received
void jerrycan_register_rx_callback(jerrycan_rx_callback_t *callback);

// Set a function to call for each received frame addressed to this board or to all boards, before the callbacks run
void jerrycan_set_host_frame_hook(void (*hook)(void));

// Send a CAN message
int jerrycan_tx(jerrycan_msg_t *msg, k_timeout_t timeout);

void jerrycan_send_ack(uint8_t uuid, int error_code);
