/**
 * @file servo.c
 * @brief JerryCAN Servo Motor Message Handling
 *
 * This file handles the reception and processing of servo motor control messages
 * recieved via the JerryCAN library. It provides functionality to move the servo,
 * as well as to read and write servo configuration settings over CAN. The module
 * integrates with the motor library for controlling and configuring servo movements.
 *
 * Key Functions:
 * - `servo_handler()`: Processes incoming servo movement commands and logs the
 *    motion parameters. (Integration with the motor library is pending)
 * - `servo_cfg_write_handler()`: Handles configuration write messages for setting
 *    servo parameters, such as minimum, middle, and maximum positions.
 * - `servo_cfg_read_handler()`: Responds to configuration read requests by sending
 *    the current servo configuration parameters back over CAN. (Actual config read
 *    from the motor library is pending)
 * - `jerrycan_servo_init()`: Registers the above message handlers and initializes
 *    the servo handling module.
 *
 * Dependencies:
 * - `jerrycan_register_rx_callback()`: Registers a CAN message callback in the JerryCAN system.
 * - `jerrycan_tx()`: Transmits CAN messages via the JerryCAN library.
 *
 * Usage:
 * This module is initialized at startup using Zephyr’s SYS_INIT macro. It registers
 * callbacks to handle servo control messages, configuration write requests, and
 * configuration read requests. Actual servo control and configuration functionality
 * will be fully realized upon integration with the high-level motor library.
 */

#include <zephyr/logging/log.h>

#include "jerrycan.h"
#include "motor_motion.h"

LOG_MODULE_DECLARE(jerrycan, LOG_LEVEL_DBG);

static void servo_handler(jerrycan_msg_t *msg) {
    // If we receive a servo message, we should move the servo

    // FIXME: Need to update this to actually integrate with the high level motion library
    LOG_INF(
        "Received servo move message: motor_id=%d, abs_or_rel=%d, position=%d, max_velocity=%d, "
        "max_acceleration=%d",
        msg->servo_move.motor_id, msg->servo_move.abs_or_rel, msg->servo_move.position, msg->servo_move.max_velocity,
        msg->servo_move.max_acceleration);
}

static jerrycan_rx_callback_t servo_callback = {
    .filter_msg_type = JERRYCAN_CMD_SERVO_MOVE,
    .func = servo_handler,
};

static void servo_cfg_write_handler(jerrycan_msg_t *msg) {
    // Ignore cfg writes that are not servo settings
    if (msg->cfg_write.type != JERRYCAN_CFG_SERVO) {
        return;
    }

    // Update the servo config settings
    LOG_INF("Received servo config write message: motor_id=%d, min_position=%d, mid_position=%d, max_position=%d",
            msg->cfg_write.servo.motor_id, msg->cfg_write.servo.min_position, msg->cfg_write.servo.mid_position,
            msg->cfg_write.servo.max_position);
}

static jerrycan_rx_callback_t servo_cfg_write_callback = {
    .filter_msg_type = JERRYCAN_CMD_CFG_WRITE,
    .func = servo_cfg_write_handler,
};

static void servo_cfg_read_handler(jerrycan_msg_t *msg) {
    // Ignore cfg reads that are not servo settings
    if (msg->cfg_write.type != JERRYCAN_CFG_SERVO) {
        return;
    }

    jerrycan_msg_t rsp;
    rsp.type = JERRYCAN_CMD_CFG_RESPONSE;
    rsp.cfg_response.type = JERRYCAN_CFG_SERVO;
    rsp.cfg_response.servo.motor_id = msg->cfg_write.servo.motor_id;

    // TODO: Read the actual servo settings from the motor library

    jerrycan_tx(&rsp, K_FOREVER);
}

static jerrycan_rx_callback_t servo_cfg_read_callback = {
    .filter_msg_type = JERRYCAN_CMD_CFG_READ,
    .func = servo_cfg_read_handler,
};

static int jerrycan_servo_init() {
    int ret;
    ret = jerrycan_register_rx_callback(&servo_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register servo callback: %d", ret);
    }

    ret = jerrycan_register_rx_callback(&servo_cfg_write_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register servo config callback: %d", ret);
    }

    ret = jerrycan_register_rx_callback(&servo_cfg_read_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register servo config read callback: %d", ret);
    }

    return 0;
}

SYS_INIT(jerrycan_servo_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
