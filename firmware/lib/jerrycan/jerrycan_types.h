#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__ZEPHYR__)
#include <cassert>
#define BUILD_ASSERT(cond, msg) static_assert(cond, msg)
typedef void* sys_snode_t;
#endif

typedef enum __attribute__((packed)) {
    JERRYCAN_CMD_ESTOP = 0x00,
    JERRYCAN_CMD_HEARTBEAT = 0x3F,
    JERRYCAN_CMD_STATUS = 0x01,
    JERRYCAN_CMD_STEPPER_MOVE = 0x02,
    JERRYCAN_CMD_SERVO_MOVE = 0x03,
    JERRYCAN_CMD_STEPPER_HOME = 0x04,
    JERRYCAN_CMD_CFG_WRITE = 0x05,
    JERRYCAN_CMD_CFG_READ = 0x06,
    JERRYCAN_CMD_CFG_RESPONSE = 0x07,
} jerrycan_cmd_type_t;

typedef struct __attribute__((packed)) {
    uint8_t payload[8];
} jerrycan_cmd_estop_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_estop_t) == 8, "jerrycan_cmd_estop_t should be 8 bytes");

typedef struct __attribute__((packed)) {
    uint8_t rsvd;
} jerrycan_cmd_heartbeat_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_heartbeat_t) == 1, "jerrycan_cmd_heartbeat_t should be 1 bytes");

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

typedef enum __attribute__((packed)) {
    JERRYCAN_CFG_STEPPER,
    JERRYCAN_CFG_SERVO,
} jerrycan_cfg_type_t;

// FIXME: This is just a placeholder- need to figure out the real settings to save for the motors
typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t motor_id : 2;
        uint8_t rsvd0 : 6;
    };
    uint16_t min_position;
    uint16_t mid_position;
    uint16_t max_position;
} jerrycan_servo_cfg_t;

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t motor_id : 2;
        uint8_t rsvd0 : 6;
    };
    uint16_t min_position;  // FIXME: Placeholder setting only- need to figure out the real ones
    uint16_t max_position;
} jerrycan_stepper_cfg_t;

typedef struct __attribute__((packed)) {
    jerrycan_cfg_type_t type;
    union {
        jerrycan_servo_cfg_t servo;
        jerrycan_stepper_cfg_t stepper;
    };
} jerrycan_cmd_cfg_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_cfg_t) == 8, "jerrycan_cmd_cfg_t should be 8 bytes");

typedef struct __attribute__((packed)) {
    jerrycan_cmd_type_t type;
    uint8_t dst_id;
    union {
        jerrycan_cmd_estop_t estop;
        jerrycan_cmd_heartbeat_t heartbeat;
        jerrycan_cmd_status_t status;
        jerrycan_cmd_stepper_move_t stepper_move;
        jerrycan_cmd_servo_move_t servo_move;
        jerrycan_cmd_cfg_t cfg_write;
        jerrycan_cmd_cfg_t cfg_response;
        jerrycan_cmd_cfg_t cfg_read;
        uint8_t payload[8];
    };
} jerrycan_msg_t;

BUILD_ASSERT(sizeof(jerrycan_msg_t) == 10, "jerrycan_msg_t should be 10 bytes");

typedef void (*jerrycan_rx_callback_fn_t)(jerrycan_msg_t* msg);

typedef struct {
    jerrycan_rx_callback_fn_t func;
    jerrycan_cmd_type_t filter_msg_type;
    sys_snode_t node;
} jerrycan_rx_callback_t;

#ifdef __cplusplus
};
#endif
