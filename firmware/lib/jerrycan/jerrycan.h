#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

typedef enum {
    JERRYCAN_CMD_ESTOP = 0x00,
    JERRYCAN_CMD_HEARTBEAT = 0x3F,
    JERRYCAN_CMD_STATUS = 0x01,
    JERRYCAN_CMD_STEPPER_MOVE = 0x02,
    JERRYCAN_CMD_SERVO_MOVE = 0x03,
} jerrycan_cmd_type_t;

typedef struct __attribute__((packed)) {
    uint8_t payload[8];
} jerrycan_cmd_estop_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_estop_t) == 8, "jerrycan_cmd_estop_t should be 8 bytes");

typedef struct __attribute__((packed)) {
} jerrycan_cmd_heartbeat_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_heartbeat_t) == 0, "jerrycan_cmd_heartbeat_t should be 0 bytes");

typedef struct __attribute__((packed)) {
    struct {
        uint8_t rsvd : 7;
        uint8_t estop_active : 1;
    };
    uint8_t stepper_status0;
    uint8_t stepper_status1;
    uint8_t stepper_status2;
    uint8_t servo_status0;
    uint8_t servo_status1;
    uint8_t servo_status2;
    struct {
        uint8_t limit_switch0 : 1;
        uint8_t limit_switch1 : 1;
        uint8_t limit_switch2 : 1;
        uint8_t button0 : 1;
        uint8_t stim0 : 1;
        uint8_t stim1 : 1;
        uint8_t stim2 : 1;
        uint8_t stim3 : 1;
    };
} jerrycan_cmd_status_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_status_t) == 8, "jerrycan_cmd_status_t should be 8 bytes");

typedef struct __attribute__((packed)) {
    struct {
        uint8_t motor_id : 2;
        uint8_t rsvd0 : 5;
        uint8_t abs_or_rel : 1;
    };
    uint8_t rsvd1;
    uint16_t position;
    uint16_t max_velocity;
    uint16_t max_acceleration;
} jerrycan_cmd_stepper_move_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_stepper_move_t) == 8, "jerrycan_cmd_stepper_move_t should be 8 bytes");

// I think the payload for this message can be the same format as the stepper move message
typedef jerrycan_cmd_stepper_move_t jerrycan_cmd_servo_move_t;

typedef struct __attribute__((packed)) {
    jerrycan_cmd_type_t type;
    union {
        jerrycan_cmd_estop_t estop;
        jerrycan_cmd_heartbeat_t heartbeat;
        jerrycan_cmd_status_t status;
        jerrycan_cmd_stepper_move_t stepper_move;
        jerrycan_cmd_servo_move_t servo_move;
        uint8_t payload[8];
    };
} jerrycan_msg_t;

// Poll for a message on the CAN bus
// Returns 0 on success, -EAGAIN if no message is available, or another negative error code on failure
int jerrycan_rx_poll(jerrycan_msg_t *msg, k_timeout_t timeout);

// Send a CAN message
int jerrycan_tx(jerrycan_msg_t *msg);

