#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>

#include "jerrycan_types.h"

// Run the main service loop for JerryCAN - RX callbacks will be processed in the context of the calling thread
// Returns 0 on success, -EAGAIN if no message is available, or another negative error code on failure
int jerrycan_run(k_timeout_t timeout);

// Register a callback to be called when a message of the specified type is received
int jerrycan_register_rx_callback(jerrycan_rx_callback_t *callback);

// Send a CAN message
int jerrycan_tx(jerrycan_msg_t *msg);
