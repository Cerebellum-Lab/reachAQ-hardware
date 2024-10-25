/**
 * @file stepper.c
 * @brief JerryCAN Stepper Motor Message Handling
 *
 * This file handles the reception and processing of stepper motor control messages
 * recieved via the JerryCAN library. It provides functionality to move the stepper,
 * as well as to read and write stepper configuration settings over CAN. The module
 * integrates with the motor library for controlling and configuring stepper movements.
 *
 * Key Functions:
 * - `stepper_handler()`: Processes incoming stepper movement commands and logs the
 *    motion parameters. Integration with the motion library is pending.
 * - `stepper_cfg_write_handler()`: Handles configuration write messages to set
 *    stepper parameters such as minimum and maximum positions.
 * - `stepper_cfg_read_handler()`: Responds to configuration read requests by sending
 *    current stepper configuration parameters back over CAN. Reading actual values
 *    from the motor library is pending.
 * - `jerrycan_stepper_init()`: Registers callbacks to handle CAN messages related
 *    to stepper movement, configuration write, and configuration read operations.
 *
 * Dependencies:
 * - `jerrycan_register_rx_callback()`: Registers a CAN message callback in the JerryCAN system.
 * - `jerrycan_tx()`: Transmits CAN messages via the JerryCAN library.
 *
 * Usage:
 * This module initializes at startup using Zephyr’s SYS_INIT macro. It registers
 * callbacks for handling movement and configuration messages related to stepper motors.
 * Future integration with the high-level motion library will allow actual motor movements
 * and configurations.
 */

#include <zephyr/logging/log.h>

#include "jerrycan.h"
#include "motor_motion.h"

LOG_MODULE_DECLARE(jerrycan, LOG_LEVEL_DBG);

static void stepper_handler(jerrycan_msg_t *msg) {
    // If we receive a stepper message, we should move the stepper

    // FIXME: Need to update this to actually integrate with the high level motion library
    LOG_INF(
        "Received stepper move message: motor_id=%d, abs_or_rel=%d, position=%d, max_velocity=%d, "
        "max_acceleration=%d",
        msg->stepper_move.motor_id, msg->stepper_move.abs_or_rel, msg->stepper_move.position,
        msg->stepper_move.max_velocity, msg->stepper_move.max_acceleration);
}

static jerrycan_rx_callback_t stepper_callback = {
    .filter_msg_type = JERRYCAN_CMD_STEPPER_MOVE,
    .func = stepper_handler,
};

static void stepper_cfg_write_handler(jerrycan_msg_t *msg) {
    // Ignore cfg writes that are not stepper settings
    if (msg->cfg_write.type != JERRYCAN_CFG_STEPPER) {
        return;
    }

    // Update the stepper config settings
    LOG_INF("Received stepper config write message: motor_id=%d, min_position=%d, max_position=%d",
            msg->cfg_write.stepper.motor_id, msg->cfg_write.stepper.min_position, msg->cfg_write.stepper.max_position);
}

static jerrycan_rx_callback_t stepper_cfg_write_callback = {
    .filter_msg_type = JERRYCAN_CMD_CFG_WRITE,
    .func = stepper_cfg_write_handler,
};

static void stepper_cfg_read_handler(jerrycan_msg_t *msg) {
    // Ignore cfg reads that are not stepper settings
    if (msg->cfg_write.type != JERRYCAN_CFG_STEPPER) {
        return;
    }

    jerrycan_msg_t rsp;
    rsp.type = JERRYCAN_CMD_CFG_RESPONSE;
    rsp.cfg_response.type = JERRYCAN_CFG_STEPPER;
    rsp.cfg_response.stepper.motor_id = msg->cfg_write.stepper.motor_id;

    // TODO: Read the actual stepper settings from the motor library

    jerrycan_tx(&rsp, K_FOREVER);
}

static jerrycan_rx_callback_t stepper_cfg_read_callback = {
    .filter_msg_type = JERRYCAN_CMD_CFG_READ,
    .func = stepper_cfg_read_handler,
};

static int jerrycan_stepper_init() {
    int ret;
    ret = jerrycan_register_rx_callback(&stepper_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register stepper callback: %d", ret);
    }

    ret = jerrycan_register_rx_callback(&stepper_cfg_write_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register stepper config callback: %d", ret);
    }

    ret = jerrycan_register_rx_callback(&stepper_cfg_read_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register stepper config read callback: %d", ret);
    }

    return 0;
}

SYS_INIT(jerrycan_stepper_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
