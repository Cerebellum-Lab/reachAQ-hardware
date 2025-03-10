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
#include "motor_common.h"
#include "motor_motion.h"
#include "motor_motion_workq.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DT_DRV_COMPAT ll_servo

/* Number of enabled servos found in the device tree */
#define SERVO_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

#define CAN_TIMEOUT K_MSEC(100)

static void servo_handler(jerrycan_msg_t *msg) {
    // If we receive a servo message, we should move the servo
    LOG_INF(
        "Received servo move message: motor_id=%d, abs_or_rel=%d, position=%f, max_velocity=%f, "
        "max_acceleration=%f",
        msg->servo_move.motor_id, msg->servo_move.abs_or_rel, (double)msg->servo_move.position,
        (double)msg->servo_move.max_velocity, (double)msg->servo_move.max_acceleration);

    const struct device *dev = servo_motor_by_id(msg->servo_move.motor_id);
    if (dev == NULL) {
        LOG_ERR("Invalid servo device number: %d", msg->servo_move.motor_id);
        return;
    }

    servo_set_parameters(dev, msg->servo_move.max_velocity, msg->servo_move.max_acceleration, 0.0f, 0.0f);

    switch (msg->servo_move.abs_or_rel) {
        case JERRYCAN_MOVE_ABSOLUTE:
            // Move the servo to the absolute position
            servo_move_to_position(dev, msg->servo_move.position);
            break;

        case JERRYCAN_MOVE_RELATIVE:
            // Move the servo to the relative position
            servo_move_relative(dev, msg->servo_move.position);
            break;

        default:
            LOG_ERR("Invalid move type: %d", msg->servo_move.abs_or_rel);
    }
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

    LOG_INF(
        "Received servo config write message: motor_id=%d, min_position=%f, max_position=%f, min_pwm_duration_us=%f, "
        "max_pwm_duration_us=%f",
        msg->cfg_write.servo.motor_id, (double)msg->cfg_write.servo.min_position,
        (double)msg->cfg_write.servo.max_position, (double)msg->cfg_write.servo.min_pwm_duration_us,
        (double)msg->cfg_write.servo.max_pwm_duration_us);

    const struct device *dev = servo_motor_by_id(msg->cfg_write.servo.motor_id);
    if (dev == NULL) {
        LOG_ERR("Invalid servo device number: %d", msg->cfg_write.servo.motor_id);
        return;
    }

    servo_set_angle_parameters(dev, msg->cfg_write.servo.min_position, msg->cfg_write.servo.max_position);
    servo_set_parameters(dev, 0.0f, 0.0f, msg->cfg_write.servo.min_pwm_duration_us,
                         msg->cfg_write.servo.max_pwm_duration_us);
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
    rsp.cfg_response.servo.error = false;

    const struct device *dev = servo_motor_by_id(msg->cfg_write.servo.motor_id);
    if (dev == NULL) {
        LOG_ERR("Invalid servo device number: %d", msg->cfg_write.servo.motor_id);
        rsp.cfg_response.servo.error = true;
        goto send_response;
    }

    float min_angle;
    float max_angle;
    float min_pwm;
    float max_pwm;

    int ret = motor_motion_servo_get_min_angle(dev, &min_angle);
    if (ret < 0) {
        LOG_ERR("Failed to get min angle: %d", ret);
        rsp.cfg_response.servo.error = true;
    }
    rsp.cfg_response.servo.min_position = min_angle;

    ret = motor_motion_servo_get_max_angle(dev, &max_angle);
    if (ret < 0) {
        LOG_ERR("Failed to get max angle: %d", ret);
        rsp.cfg_response.servo.error = true;
    }
    rsp.cfg_response.servo.max_position = max_angle;

    ret = motor_motion_servo_get_min_angle_pwm(dev, &min_pwm);
    if (ret < 0) {
        LOG_ERR("Failed to get min pwm: %d", ret);
        rsp.cfg_response.servo.error = true;
    }
    rsp.cfg_response.servo.min_pwm_duration_us = min_pwm;

    ret = motor_motion_servo_get_max_angle_pwm(dev, &max_pwm);
    if (ret < 0) {
        LOG_ERR("Failed to get max pwm: %d", ret);
        rsp.cfg_response.servo.error = true;
    }
    rsp.cfg_response.servo.max_pwm_duration_us = max_pwm;

send_response:
    jerrycan_tx(&rsp, K_NO_WAIT);
}

static jerrycan_rx_callback_t servo_cfg_read_callback = {
    .filter_msg_type = JERRYCAN_CMD_CFG_READ,
    .func = servo_cfg_read_handler,
};

static void jerrycan_servo_status_tx() {
    for (int motor_id = 0; motor_id < SERVO_COUNT; motor_id++) {
        const struct device *servo = servo_motor_by_id(motor_id);

        if (servo == NULL) {
            LOG_WRN("Failed to retreive servo for servo_status_tx: Invalid servo device number: %d", motor_id);
            continue;
        }
        struct servo_work_context *context = find_servo_context_from_device(servo);
        float position = context->context.last_position_generated;
        jerrycan_msg_t msg = {
            .type = JERRYCAN_CMD_SERVO_STATUS,
            .servo_status = {
                .motor_id = motor_id,
                .status = 0,  // ll_motor_get_status(servo),  // FIXME: Needs to be implemented or removed
                .position = position,
            }};

        /* Transmit message */
        int ret = jerrycan_tx(&msg, K_NO_WAIT);
        if (ret != 0) {
            LOG_WRN("Failed to send servo Status CAN message for servo%d: %d", motor_id, ret);
        }
    }
}

K_TIMER_DEFINE(jerrycan_servo_status_tx_timer, jerrycan_servo_status_tx, NULL);

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

    /* Start timer to send the servo status messages periodically */
    k_timer_start(&jerrycan_servo_status_tx_timer, K_MSEC(100), K_MSEC(CONFIG_LIB_JERRYCAN_SERVO_STATUS_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(jerrycan_servo_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
