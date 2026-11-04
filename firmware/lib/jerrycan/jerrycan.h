#pragma once

#include <zephyr/kernel.h>

#include "jerrycan_types.h"

/*
    Board clock: microseconds since boot, from the 64-bit CPU cycle counter. The same
    clock stamps every timing trailer. Safe in any context, including interrupts.
*/
uint64_t jerrycan_board_time_us(void);

/* Convert a k_cycle_get_64() reading taken elsewhere (e.g. in a driver ISR) to board time. */
uint64_t jerrycan_cycles_to_board_time_us(uint64_t cycles);

/* Random per boot. Reported in CAPABILITIES_RESPONSE and in every timing trailer. */
uint32_t jerrycan_boot_id(void);

/*
    Board time when the CAN RX interrupt handled the frame now being processed. Valid
    only inside a jerrycan RX callback.
*/
uint64_t jerrycan_rx_time_us(void);

// Run the main service loop for JerryCAN - RX callbacks will be processed in the context of the calling thread
// Returns 0 on success, -EAGAIN if no message is available, or another negative error code on failure
int jerrycan_run(k_timeout_t timeout);

// Register a callback to be called when a message of the specified type is received
// In the callback, msg->dst_id is the low 5 bits of the received CAN id: this board's own address or 0x1F
// (broadcast) for host commands, and the sender's address for board-originated frames such as claps.
void jerrycan_register_rx_callback(jerrycan_rx_callback_t *callback);

// Send a CAN message
int jerrycan_tx(jerrycan_msg_t *msg, k_timeout_t timeout);

/*
    Queue a message whose timing trailer reports `kind` at `board_time_us`, instead of
    the moment it was queued (which is what jerrycan_tx() reports, as TX_QUEUE).
    Use it when the board knows when the thing the message describes happened.
    With CONFIG_LIB_JERRYCAN_TIMING off there is no trailer, so kind, flags and
    board_time_us are ignored and it queues the message as jerrycan_tx() does.
*/
int jerrycan_tx_timed(jerrycan_msg_t *msg, jerrycan_timestamp_kind_t kind, uint8_t flags, uint64_t board_time_us,
                      k_timeout_t timeout);

void jerrycan_send_ack(uint8_t uuid, int error_code);
