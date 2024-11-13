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

#define JERRYCAN_MAX_PAYLOAD_SIZE 64

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
    JERRYCAN_CMD_PRESSURE_READ = 0x08,
    JERRYCAN_CMD_TEMP_HUM_READ = 0x09,
    JERRYCAN_CMD_GPIO_READ = 0x0A,
    JERRYCAN_CMD_GPIO_WRITE = 0x0B,
    JERRYCAN_CMD_TONE = 0X0C,
    JERRYCAN_CMD_ANALOG_OUT = 0X0D,
    JERRYCAN_CMD_LOAD_CELL_READ = 0x0E,
    JERRYCAN_CMD_DOOR_SENSOR = 0x0F,
    JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_BEGIN = 0x10,
    JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_CONT = 0x11,
    JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_END = 0x12,
    JERRYCAN_CMD_RGB_LED = 0x13,
    JERRYCAN_CMD_LOAD_CELL_TARE = 0x14,
    JERRYCAN_CMD_PRESSURE_SENSOR_TARE = 0x15,
    JERRYCAN_CMD_STEPPER_STATUS = 0x16,
    JERRYCAN_CMD_SERVO_STATUS = 0x17,
    JERRYCAN_CMD_MIN = 0x00,
    JERRYCAN_CMD_MAX = 0x3F,
} jerrycan_cmd_type_t;

typedef struct __attribute__((packed)) {
    uint8_t payload;
} jerrycan_cmd_estop_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_estop_t) == 1, "jerrycan_cmd_estop_t should be 1 bytes");

typedef struct __attribute__((packed)) {
    uint8_t rsvd;
} jerrycan_cmd_heartbeat_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_heartbeat_t) == 1, "jerrycan_cmd_heartbeat_t should be 1 bytes");

typedef struct __attribute__((packed)) {
    struct {
        uint8_t estop_active : 1;
        uint8_t limit_switch0 : 1;
        uint8_t limit_switch1 : 1;
        uint8_t limit_switch2 : 1;
        uint8_t button0 : 1;
        uint8_t rsvd0 : 3;
    };
    uint8_t rsvd1;
    uint8_t stepper_status0;
    uint8_t stepper_status1;
    uint8_t stepper_status2;
    uint8_t servo_status0;
    uint8_t servo_status1;
    uint8_t servo_status2;
} jerrycan_cmd_status_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_status_t) == 8, "jerrycan_cmd_status_t should be 8 bytes");

typedef enum __attribute__((packed)) {
    JERRYCAN_MOVE_ABSOLUTE = 0,
    JERRYCAN_MOVE_RELATIVE = 1,
} abs_or_rel_t;

typedef struct __attribute__((packed)) {
    struct {
        uint8_t motor_id : 2;
        uint8_t rsvd0 : 5;
        abs_or_rel_t abs_or_rel : 1;
    };
    uint8_t rsvd1;
    int16_t position;
    uint16_t max_velocity;
    uint16_t max_acceleration;
} jerrycan_cmd_stepper_move_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_stepper_move_t) == 8, "jerrycan_cmd_stepper_move_t should be 8 bytes");

typedef struct __attribute__((packed)) {
    uint8_t motor_id : 7;
    uint8_t forward : 1;
} jerrycan_cmd_stepper_home_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_stepper_home_t) == 1, "jerrycan_cmd_stepper_home_t should be 1 bytes");

// I think the payload for this message can be the same format as the stepper move message
typedef jerrycan_cmd_stepper_move_t jerrycan_cmd_servo_move_t;

typedef enum __attribute__((packed)) {
    JERRYCAN_CFG_STEPPER,
    JERRYCAN_CFG_SERVO,
} jerrycan_cfg_type_t;

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t motor_id : 2;
        uint8_t error : 1;
        uint8_t rsvd0 : 5;
    };
    int16_t min_position;
    int16_t max_position;
    uint16_t min_pwm_duration_us;
    uint16_t max_pwm_duration_us;
} jerrycan_servo_cfg_t;

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t motor_id : 2;
        uint8_t error : 1;
        uint8_t rsvd0 : 5;
    };
    uint16_t min_step_inverse;  // Power of 2 representing microstepping. Thus 2 represents 1/2 steps, 4 represents 1/4
                                // steps, etc.
    uint16_t steps_per_revolution;
} jerrycan_stepper_cfg_t;

typedef struct __attribute__((packed)) {
    jerrycan_cfg_type_t type;
    union {
        jerrycan_servo_cfg_t servo;
        jerrycan_stepper_cfg_t stepper;
    };
} jerrycan_cmd_cfg_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_cfg_t) == 10, "jerrycan_cmd_cfg_t should be 10 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint8_t error;
    uint32_t pressure_mv;
} jerrycan_cmd_pressure_read_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_pressure_read_t) == 6, "jerrycan_cmd_pressure_read_t should be 6 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
} jerrycan_cmd_pressure_sensor_tare_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_pressure_sensor_tare_t) == 1, "jerrycan_cmd_pressure_sensor_tare_t should be 1 bytes");

/*
    Scale factor for temperature and humidity transmission
    (provides 2 significant decimal digits of precision)
*/
#define TEMPHUM_SCALE_FACTOR 100

typedef struct __attribute__((packed)) {
    uint8_t instance;
    /*
        SI7021 Temperature Range -40C to +125C - Sensor data temperature field is a float.
        This field will contain (uint16_t)(temperature * 100) to provided two decimal
        places of precision without bloating the struct beyond the 8 byte limit. As such, the
        recieving end must perform (float)(temperature / 100.0) to acquire the temperature value.
    */
    uint16_t temperature;
    /*
        SI7021 Humidity Range 0% to 100% - Sensor data humidity field is a float.
        This field will contain (uint16_t)(humidity * 100) to provided two decimal
        places of precision without bloating the struct beyond the 8 byte limit. As such, the
        recieving end must perform (float)(humidity / 100.0) to acquire the humidity value.
    */
    uint16_t humidity;
} jerrycan_cmd_temp_hum_read_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_temp_hum_read_t) == 5, "jerrycan_cmd_temp_hum_read_t should be 5 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint32_t state;
} jerrycan_cmd_gpio_read_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_gpio_read_t) == 5, "jerrycan_cmd_gpio_read_t should be 5 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint16_t gpio_idx;
    bool state : 1;
    uint8_t rsvd0 : 7;
} jerrycan_cmd_gpio_write_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_gpio_write_t) == 4, "jerrycan_cmd_gpio_write_t should be 4 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint16_t frequency_hz;
    uint16_t duration_ms;
} jerrycan_cmd_tone_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_tone_t) == 5, "jerrycan_cmd_tone_t should be 5 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
    uint16_t value_mv;
} jerrycan_cmd_analog_out_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_analog_out_t) == 3, "jerrycan_cmd_analog_out_t should be 3 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
    float load_mv;
} jerrycan_cmd_load_cell_read_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_load_cell_read_t) == 5, "jerrycan_cmd_load_cell_read_t should be 5 bytes");

#define DOOR_SENSOR_COUNT 3

typedef struct __attribute__((packed)) {
    uint8_t opened : DOOR_SENSOR_COUNT;  // bit flags: opened(1), closed(0)
} jerrycan_cmd_door_closed_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_door_closed_t) == 1, "jerrycan_cmd_door_closed_t should be 1 bytes");

typedef struct __attribute__((packed)) {
    uint8_t red;    // (%)
    uint8_t green;  // (%)
    uint8_t blue;   // (%)
} jerrycan_cmd_rgb_led_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_rgb_led_t) == 3, "jerrycan_cmd_rgb_led_t should be 3 bytes");

typedef struct __attribute__((packed)) {
    uint32_t stream_id;
} jerrycan_cmd_audio_data_cmd_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_audio_data_cmd_t) == 4, "jerrycan_cmd_audio_data_cmd_t should be 4 bytes");

typedef struct __attribute__((packed)) {
    uint8_t payload[JERRYCAN_MAX_PAYLOAD_SIZE];
} jerrycan_cmd_audio_data_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_audio_data_t) == JERRYCAN_MAX_PAYLOAD_SIZE,
             "jerrycan_cmd_audio_data_t should be 64 bytes");

typedef struct __attribute__((packed)) {
    uint8_t instance;
} jerrycan_cmd_load_cell_tare_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_load_cell_tare_t) == 1, "jerrycan_cmd_load_cell_tare_t should be 1 bytes");

typedef struct __attribute__((packed)) {
    uint8_t motor_id;
    uint8_t status;
    uint8_t homing_status;
    uint8_t limit_switch;
    int32_t position;
} jerrycan_cmd_stepper_status_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_stepper_status_t) == 8, "jerrycan_cmd_stepper_status_t should be 8 bytes");

typedef struct __attribute__((packed)) {
    uint8_t motor_id;
    uint8_t status;
    int32_t position;
} jerrycan_cmd_servo_status_t;

BUILD_ASSERT(sizeof(jerrycan_cmd_servo_status_t) == 6, "jerrycan_cmd_servo_status_t should be 6 bytes");

typedef struct __attribute__((packed)) {
    jerrycan_cmd_type_t type;
    uint8_t dst_id;
    union {
        jerrycan_cmd_estop_t estop;
        jerrycan_cmd_heartbeat_t heartbeat;
        jerrycan_cmd_status_t status;
        jerrycan_cmd_stepper_move_t stepper_move;
        jerrycan_cmd_servo_move_t servo_move;
        jerrycan_cmd_stepper_home_t stepper_home;
        jerrycan_cmd_cfg_t cfg_write;
        jerrycan_cmd_cfg_t cfg_response;
        jerrycan_cmd_cfg_t cfg_read;
        jerrycan_cmd_stepper_status_t stepper_status;
        jerrycan_cmd_servo_status_t servo_status;
        jerrycan_cmd_pressure_read_t pressure_read;
        jerrycan_cmd_temp_hum_read_t temp_hum_read;
        jerrycan_cmd_gpio_read_t gpio_read;
        jerrycan_cmd_gpio_write_t gpio_write;
        jerrycan_cmd_tone_t tone;
        jerrycan_cmd_analog_out_t analog_out;
        jerrycan_cmd_load_cell_read_t load_cell_read;
        jerrycan_cmd_door_closed_t doors;
        jerrycan_cmd_audio_data_cmd_t audio_data_cmd;
        jerrycan_cmd_audio_data_t audio_data;
        jerrycan_cmd_rgb_led_t rgb_led;
        jerrycan_cmd_load_cell_tare_t load_cell_tare;
        jerrycan_cmd_pressure_sensor_tare_t pressure_sensor_tare;
        uint8_t payload[JERRYCAN_MAX_PAYLOAD_SIZE];
    };
} jerrycan_msg_t;

BUILD_ASSERT(sizeof(jerrycan_msg_t) == JERRYCAN_MAX_PAYLOAD_SIZE + sizeof(jerrycan_cmd_type_t) + sizeof(uint8_t),
             "jerrycan_msg_t size should be max payload size + header fields size");

typedef void (*jerrycan_rx_callback_fn_t)(jerrycan_msg_t* msg);

typedef struct {
    jerrycan_rx_callback_fn_t func;
    jerrycan_cmd_type_t filter_msg_type;
    sys_snode_t node;
} jerrycan_rx_callback_t;

#ifdef __cplusplus
};
#endif
