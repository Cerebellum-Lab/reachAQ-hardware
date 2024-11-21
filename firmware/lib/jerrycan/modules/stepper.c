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

#include "stepper.h"

#include <zephyr/logging/log.h>

#include "jerrycan.h"
#include "motor_motion.h"
#include "motor_motion_workq.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define DT_DRV_COMPAT ll_stepper

/* Number of enabled steppers found in the device tree */
#define STEPPER_COUNT DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

#define CAN_TIMEOUT K_MSEC(100)

static void stepper_handler(jerrycan_msg_t *msg) {
    // If we receive a stepper message, we should move the stepper
    LOG_INF(
        "Received stepper move message: motor_id=%d, abs_or_rel=%d, position=%f, max_velocity=%f, "
        "max_acceleration=%f",
        msg->stepper_move.motor_id, msg->stepper_move.abs_or_rel, msg->stepper_move.position,
        msg->stepper_move.max_velocity, msg->stepper_move.max_acceleration);

    const struct device *dev = stepper_motor_by_id(msg->stepper_move.motor_id);
    if (dev == NULL) {
        LOG_ERR("Invalid stepper device number: %d", msg->stepper_move.motor_id);
        return;
    }

    stepper_set_parameters(dev, msg->stepper_move.max_velocity, msg->stepper_move.max_acceleration, 0.0f, 0.0f);

    switch (msg->stepper_move.abs_or_rel) {
        case JERRYCAN_MOVE_ABSOLUTE:
            // Move the stepper to the absolute position
            stepper_move_to_position(dev, msg->stepper_move.position);
            break;

        case JERRYCAN_MOVE_RELATIVE:
            // Move the stepper to the relative position
            stepper_move_relative(dev, msg->stepper_move.position);
            break;

        default:
            LOG_ERR("Invalid move type: %d", msg->stepper_move.abs_or_rel);
    }
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

    LOG_INF("Received stepper config write message: motor_id=%d, min_step_inverse=%f, steps_per_revolution=%f",
            msg->cfg_write.stepper.motor_id, msg->cfg_write.stepper.min_step_inverse,
            msg->cfg_write.stepper.steps_per_revolution);

    const struct device *dev = stepper_motor_by_id(msg->cfg_write.stepper.motor_id);
    if (dev == NULL) {
        LOG_ERR("Invalid stepper device number: %d", msg->cfg_write.stepper.motor_id);
        return;
    }

    stepper_set_parameters(dev, 0.0f, 0.0f, 1.0f / msg->cfg_write.stepper.min_step_inverse,
                           msg->cfg_write.stepper.steps_per_revolution);
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
    rsp.cfg_response.stepper.error = false;

    int ret;
    float min_step;
    float steps_per_revolution;
    const struct device *dev = stepper_motor_by_id(msg->cfg_write.stepper.motor_id);
    if (dev == NULL) {
        LOG_ERR("Invalid stepper device number: %d", msg->cfg_write.stepper.motor_id);
        rsp.cfg_response.stepper.error = true;
        goto send_response;
    }

    ret = motor_motion_stepper_get_min_step(dev, &min_step);
    if (ret < 0) {
        LOG_ERR("Failed to get min step: %d", ret);
        rsp.cfg_response.stepper.error = true;
    }
    rsp.cfg_response.stepper.min_step_inverse = (1.0f / min_step);

    ret = motor_motion_stepper_get_steps_per_revolution(dev, &steps_per_revolution);
    if (ret < 0) {
        LOG_ERR("Failed to get steps per revolution: %d", ret);
        rsp.cfg_response.stepper.error = true;
    }
    rsp.cfg_response.stepper.steps_per_revolution = steps_per_revolution;

send_response:
    jerrycan_tx(&rsp, K_NO_WAIT);
}

static jerrycan_rx_callback_t stepper_cfg_read_callback = {
    .filter_msg_type = JERRYCAN_CMD_CFG_READ,
    .func = stepper_cfg_read_handler,
};

static void stepper_home_handler(jerrycan_msg_t *msg) {
    const struct device *dev = stepper_motor_by_id(msg->stepper_home.motor_id);
    if (dev == NULL) {
        LOG_ERR("Failed to home stepper motor: Invalid stepper device number - %d", msg->stepper_home.motor_id);
        return;
    }

    int ret = stepper_go_home_slowly(dev, msg->stepper_home.forward);
    if (ret < 0) {
        LOG_ERR("Failed to home stepper motor: %d", ret);
    }
}

static jerrycan_rx_callback_t stepper_home_callback = {
    .filter_msg_type = JERRYCAN_CMD_STEPPER_HOME,
    .func = stepper_home_handler,
};

static void jerrycan_stepper_status_tx(const struct device *stepper) {
    struct stepper_work_context *context = find_stepper_context_from_device(stepper);

    uint8_t motor_id = ll_motor_get_id(stepper);
    float position = context->context.last_position_generated;

    jerrycan_msg_t msg = {.type = JERRYCAN_CMD_STEPPER_STATUS,
                          .stepper_status = {
                              .motor_id = motor_id,
                              .status = 0,  // ll_motor_get_status(stepper), FIXME: Needs to be implemented or removed
                              .homing_status = 0,  // FIXME: Needs to be implemented or removed
                              .position = position,
                              .limit_switch = ll_stepper_get_limit_switch_state(stepper),
                          }};

    /* Transmit message */
    int ret = jerrycan_tx(&msg, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("Failed to send Stepper Status CAN message for stepper%d: %d", motor_id, ret);
    }
}

static void jerrycan_bulk_stepper_status_tx() {
    for (int motor_id = 0; motor_id < STEPPER_COUNT; motor_id++) {
        const struct device *stepper = stepper_motor_by_id(motor_id);

        if (stepper == NULL) {
            LOG_WRN("Failed to retrieve stepper for stepper_status_tx: Invalid stepper device number: %d", motor_id);
            return;
        }

        jerrycan_stepper_status_tx(stepper);
    }
}

static void jerrycan_limit_switch_handler(const struct device *dev, ll_motor_events_t event, void *arg,
                                          void *user_data) {
    // Send a status message if the limit switch is tripped
    if (event == LL_MOTOR_EVENT_LIMIT_SWITCH) {
        jerrycan_stepper_status_tx(dev);
    }
}

#define STEPPER_LIMIT_SWITCH_CB(_)             \
    {                                          \
        .func = jerrycan_limit_switch_handler, \
        .user_data = NULL,                     \
        .node = {0},                           \
    }

#define STEPPER_LIMIT_SWITCH_CB_COMMA(idx) STEPPER_LIMIT_SWITCH_CB(idx),

static ll_stepper_cb_t stepper_limit_switch_callbacks[STEPPER_COUNT] = {
    DT_FOREACH_STATUS_OKAY(ll_stepper, STEPPER_LIMIT_SWITCH_CB_COMMA)};

#define INSTALL_STEPPER_LIMIT_SWITCH_CB(idx)                                         \
    do {                                                                             \
        const struct device *stepper = stepper_motor_by_id(idx);                     \
        ll_stepper_register_callback(stepper, &stepper_limit_switch_callbacks[idx]); \
    } while (0)

K_TIMER_DEFINE(jerrycan_stepper_status_tx_timer, jerrycan_bulk_stepper_status_tx, NULL);

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

    ret = jerrycan_register_rx_callback(&stepper_home_callback);
    if (ret < 0) {
        LOG_WRN("Failed to register stepper home callback: %d", ret);
    }

    // Install limit switch callback for each stepper motor
    for (int motor_id = 0; motor_id < STEPPER_COUNT; motor_id++) {
        INSTALL_STEPPER_LIMIT_SWITCH_CB(motor_id);
    }

    /* Start timer to send the stepper status messages periodically */
    k_timer_start(&jerrycan_stepper_status_tx_timer, K_MSEC(100),
                  K_MSEC(CONFIG_LIB_JERRYCAN_STEPPER_STATUS_TX_PERIOD_MS));

    return 0;
}

SYS_INIT(jerrycan_stepper_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
