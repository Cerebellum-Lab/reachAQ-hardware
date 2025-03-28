#include "libjerrycan.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "jerrycan_types.h"

int JerryCAN::Open() {
    // Open the CAN socket
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        spdlog::error("Failed to open CAN socket: {}", errno);
        return -EIO;
    }

    spdlog::info("Opened CAN socket: {}", s);

    // Set a timeout on the socket so reads don't block forever
    timeval tv = {0};
    tv.tv_sec = 0;
    tv.tv_usec = 1000;  // 1ms
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int canfd_enabled = 1;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_enabled, sizeof(canfd_enabled));

    // Find the CAN interface index
    struct ifreq ifr;
    strcpy(ifr.ifr_name, "can0");  // FIXME: Make this configurable
    ioctl(s, SIOCGIFINDEX, &ifr);

    // Bind to the CAN sockets
    struct sockaddr_can addr = {0};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    int ret = bind(s, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        spdlog::error("Failed to bind CAN socket: {}", errno);
        return -EIO;
    }

    spdlog::info("Bound CAN socket");

    _can_socket_handle = s;

    return 0;
}

int JerryCAN::Close() {
    // Close the CAN socket
    int ret = close(_can_socket_handle);
    if (ret < 0) {
        spdlog::error("Failed to close CAN socket: {}", errno);
        return -errno;
    }

    spdlog::info("Closed CAN socket: {}", _can_socket_handle);
    _can_socket_handle = -1;

    return 0;
}

// Return the payload size for a given message type
static uint8_t jerrycan_msg_get_payload_size(jerrycan_cmd_type_t msg_type) {
    switch (msg_type) {
        case JERRYCAN_CMD_ESTOP:
            return sizeof(jerrycan_cmd_estop_t);
        case JERRYCAN_CMD_HEARTBEAT:
            return sizeof(jerrycan_cmd_heartbeat_t);
        case JERRYCAN_CMD_STATUS:
            return sizeof(jerrycan_cmd_status_t);
        case JERRYCAN_CMD_STEPPER_MOVE:
            return sizeof(jerrycan_cmd_stepper_move_t);
        case JERRYCAN_CMD_SERVO_MOVE:
            return sizeof(jerrycan_cmd_servo_move_t);
        case JERRYCAN_CMD_STEPPER_STATUS:
            return sizeof(jerrycan_cmd_stepper_status_t);
        case JERRYCAN_CMD_SERVO_STATUS:
            return sizeof(jerrycan_cmd_servo_status_t);
        case JERRYCAN_CMD_STEPPER_HOME:
            return sizeof(jerrycan_cmd_stepper_home_t);
        case JERRYCAN_CMD_CFG_WRITE:
            return sizeof(jerrycan_cmd_cfg_t);
        case JERRYCAN_CMD_CFG_RESPONSE:
            return sizeof(jerrycan_cmd_cfg_t);
        case JERRYCAN_CMD_CFG_READ:
            return sizeof(jerrycan_cmd_cfg_t);
        case JERRYCAN_CMD_PRESSURE_READ:
            return sizeof(jerrycan_cmd_pressure_read_t);
        case JERRYCAN_CMD_TEMP_HUM_READ:
            return sizeof(jerrycan_cmd_temp_hum_read_t);
        case JERRYCAN_CMD_GPIO_READ:
            return sizeof(jerrycan_cmd_gpio_read_t);
        case JERRYCAN_CMD_GPIO_WRITE:
            return sizeof(jerrycan_cmd_gpio_write_t);
        case JERRYCAN_CMD_TONE:
            return sizeof(jerrycan_cmd_tone_t);
        case JERRYCAN_CMD_ANALOG_OUT:
            return sizeof(jerrycan_cmd_analog_out_t);
        case JERRYCAN_CMD_LOAD_CELL_READ:
            return sizeof(jerrycan_cmd_load_cell_read_t);
        case JERRYCAN_CMD_LOAD_CELL_TARE:
            return sizeof(jerrycan_cmd_load_cell_tare_t);
        case JERRYCAN_CMD_PRESSURE_SENSOR_TARE:
            return sizeof(jerrycan_cmd_pressure_sensor_tare_t);
        case JERRYCAN_CMD_RGB_LED:
            return sizeof(jerrycan_cmd_rgb_led_t);
        case JERRYCAN_CMD_DOOR_SENSOR:
            return sizeof(jerrycan_cmd_door_closed_t);
        case JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_BEGIN:
            return sizeof(jerrycan_cmd_audio_data_cmd_t);
        case JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_CONT:
            return sizeof(jerrycan_cmd_audio_data_t);
        case JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_END:
            return sizeof(jerrycan_cmd_audio_data_cmd_t);
        case JERRYCAN_CMD_BOOTLOADER_COMMAND:
            return sizeof(jerrycan_cmd_bootloader_command_t);
        case JERRYCAN_CMD_BOOTLOADER_RESPONSE:
            return sizeof(jerrycan_cmd_bootloader_response_t);
        case JERRYCAN_CMD_BOOTLOADER_DATA:
            return sizeof(jerrycan_cmd_bootloader_data_t);
        default:
            return 0;
    }
}

// Convert from number of bytes to Data Length Code (DLC)
static inline uint8_t can_bytes_to_dlc(uint8_t num_bytes) {
    return num_bytes <= 8    ? num_bytes
           : num_bytes <= 12 ? 9
           : num_bytes <= 16 ? 10
           : num_bytes <= 20 ? 11
           : num_bytes <= 24 ? 12
           : num_bytes <= 32 ? 13
           : num_bytes <= 48 ? 14
                             : 15;
}

// Convert from Data Length Code (DLC) to the number of data bytes
static inline uint8_t can_dlc_to_bytes(uint8_t dlc) {
    static const uint8_t dlc_table[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    static const uint8_t dlc_table_len = (sizeof(dlc_table) / sizeof(dlc_table[0])) - 1;

    if (dlc < dlc_table_len) {
        return dlc_table[dlc];
    } else {
        return dlc_table[dlc_table_len];
    }
}

int JerryCAN::SendMessage(jerrycan_msg_t &msg, uint16_t dst_id) {
    // Send the message
    struct canfd_frame frame = {0};
    uint8_t payload_size = jerrycan_msg_get_payload_size(msg.type);
    frame.can_id = ((msg.type & 0x3F) << 5) | (dst_id & 0x1F);
    frame.len = payload_size;

    memcpy(frame.data, msg.payload, payload_size);

    // For payload sizes > 8, DLC no longer maps 1:1 to the number of bytes in the payload
    // If the actual payload size is less than the number of bytes indicated by DLC, pad the rest with 0
    uint8_t dlc_bytes = can_dlc_to_bytes(can_bytes_to_dlc(payload_size));
    memset(&frame.data[payload_size], 0, dlc_bytes - payload_size);

    auto ret = write(_can_socket_handle, &frame, sizeof(frame));
    if (ret < 0) {
        spdlog::error("Failed to send CAN message: {}", errno);
        return -errno;
    }

    return 0;
}

int JerryCAN::ReceiveMessage(jerrycan_msg_t &msg) {
    // Receive a message
    struct canfd_frame frame = {0};
    auto ret = read(_can_socket_handle, &frame, sizeof(frame));
    if (ret <= 0) {
        if (errno != EAGAIN) {
            spdlog::error("Failed to receive CAN message: {}", errno);
        }
        return -errno;
    }

    msg.type = static_cast<jerrycan_cmd_type_t>((frame.can_id >> 5) & 0x3F);
    msg.dst_id = frame.can_id & 0x1F;
    uint8_t msg_len = jerrycan_msg_get_payload_size(msg.type);
    memcpy(msg.payload, frame.data, msg_len);

    return 0;
}

int JerryCAN::Heartbeat() {
    // Send a heartbeat message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_HEARTBEAT,
        .heartbeat =
            {
                .rsvd = 0xFF,
            },
    };

    return SendMessage(msg, 0x1F);
}

int JerryCAN::EStop(bool enable) {
    // Send an emergency stop message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_ESTOP,
        .estop =
            {
                .payload = 0xFF,
            },
    };

    return SendMessage(msg, 0x1F);
}

int JerryCAN::StepperMove(uint8_t dst_id, uint8_t motor_id, float position, float max_velocity, float max_acceleration,
                          abs_or_rel_t abs_or_rel) {
    // Send a stepper move message
    jerrycan_msg_t msg;
    msg.type = JERRYCAN_CMD_STEPPER_MOVE;
    msg.stepper_move.motor_id = motor_id;
    msg.stepper_move.rsvd0 = 0;
    msg.stepper_move.abs_or_rel = abs_or_rel;
    msg.stepper_move.position = position;
    msg.stepper_move.max_velocity = max_velocity;
    msg.stepper_move.max_acceleration = max_acceleration;

    return SendMessage(msg, dst_id);
}

int JerryCAN::ServoMove(uint8_t dst_id, uint8_t motor_id, float position, float max_velocity, float max_acceleration,
                        abs_or_rel_t abs_or_rel) {
    // Send a servo move message
    jerrycan_msg_t msg;
    msg.type = JERRYCAN_CMD_SERVO_MOVE;
    msg.servo_move.motor_id = motor_id;
    msg.servo_move.rsvd0 = 0;
    msg.servo_move.abs_or_rel = abs_or_rel;
    msg.servo_move.position = position;
    msg.servo_move.max_velocity = max_velocity;
    msg.servo_move.max_acceleration = max_acceleration;

    return SendMessage(msg, dst_id);
}

int JerryCAN::StepperHome(uint8_t dst_id, uint8_t motor_id) {
    // Send a stepper home message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_STEPPER_HOME,
        .stepper_home =
            {
                .motor_id = motor_id,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::CfgWrite(uint8_t dst_id, jerrycan_cmd_cfg_t &cfg) {
    // Send a Configuration Write Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_CFG_WRITE,
        .cfg_write = cfg,
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::CfgRead(uint8_t dst_id, jerrycan_cmd_cfg_t &cfg) {
    // Send a Configuration Read Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_CFG_READ,
        .cfg_read = cfg,
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::StepperCfgWrite(uint8_t dst_id, uint8_t motor_id, uint16_t min_step_inverse, float steps_per_revolution,
                              float motor_max_velocity, float motor_max_acceleration, bool flip_limit_orientation) {
    jerrycan_cmd_cfg_t cfg_write = {.type = JERRYCAN_CFG_STEPPER,
                                    .stepper = {
                                        .motor_id = motor_id,
                                        .flip_limit_orientation = flip_limit_orientation,
                                        .min_step_inverse = min_step_inverse,
                                        .steps_per_revolution = steps_per_revolution,
                                        .motor_max_velocity = motor_max_velocity,
                                        .motor_max_acceleration = motor_max_acceleration,
                                    }};

    return CfgWrite(dst_id, cfg_write);
}

int JerryCAN::ServoCfgWrite(uint8_t dst_id, uint8_t motor_id, float min_position, float max_position,
                            float min_pwm_duration_us, float max_pwm_duration_us) {
    jerrycan_cmd_cfg_t cfg_write = {.type = JERRYCAN_CFG_SERVO,
                                    .servo = {
                                        .motor_id = motor_id,
                                        .min_position = min_position,
                                        .max_position = max_position,
                                        .min_pwm_duration_us = min_pwm_duration_us,
                                        .max_pwm_duration_us = max_pwm_duration_us,
                                    }};

    return CfgWrite(dst_id, cfg_write);
}

int JerryCAN::StepperCfgRead(uint8_t dst_id, uint8_t motor_id) {
    jerrycan_cmd_cfg_t cfg_read = {
        .type = JERRYCAN_CFG_STEPPER,
        .stepper =
            {
                .motor_id = motor_id,
            },
    };

    return CfgRead(dst_id, cfg_read);
}

int JerryCAN::ServoCfgRead(uint8_t dst_id, uint8_t motor_id) {
    jerrycan_cmd_cfg_t cfg_read = {
        .type = JERRYCAN_CFG_SERVO,
        .servo =
            {
                .motor_id = motor_id,
            },
    };

    return CfgRead(dst_id, cfg_read);
}

int JerryCAN::GPIOWrite(uint8_t dst_id, uint8_t instance, uint16_t gpio_idx, bool state) {
    // Send a GPIO Write Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_GPIO_WRITE,
        .gpio_write =
            {
                .instance = instance,
                .gpio_idx = gpio_idx,
                .state = state,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::ToneWrite(uint8_t dst_id, uint8_t instance, uint16_t frequency, uint16_t duration) {
    // Send a GPIO Write Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_TONE,
        .tone =
            {
                .instance = instance,
                .frequency_hz = frequency,
                .duration_ms = duration,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::AnalogOutWrite(uint8_t dst_id, uint8_t instance, uint16_t value_mv) {
    // Send an Analog Out Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_ANALOG_OUT,
        .analog_out =
            {
                .instance = instance,
                .value_mv = value_mv,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::LoadCellTare(uint8_t dst_id, uint8_t instance) {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_LOAD_CELL_TARE,
        .load_cell_tare =
            {
                .instance = instance,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::PressureSensorTare(uint8_t dst_id, uint8_t instance) {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_PRESSURE_SENSOR_TARE,
        .pressure_sensor_tare =
            {
                .instance = instance,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::RGBLEDWrite(uint8_t dst_id, uint8_t red, uint8_t green, uint8_t blue) {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_RGB_LED,
        .rgb_led =
            {
                .red = red,
                .green = green,
                .blue = blue,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::BootloaderCommand(uint8_t dst_id, jerrycan_bootloader_subcmd_t subcmd) {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_BOOTLOADER_COMMAND,
        .bootloader_command =
            {
                .type = subcmd,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::BootloaderData(uint8_t dst_id, jerrycan_cmd_bootloader_data_t &data) {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_BOOTLOADER_DATA,
    };

    msg.bootloader_data = data;

    return SendMessage(msg, dst_id);
}
