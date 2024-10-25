#include "libjerrycan.h"

#include <linux/can.h>
#include <net/if.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

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

int JerryCAN::SendMessage(jerrycan_msg_t &msg, uint16_t dst_id) {
    // Send the message
    struct can_frame frame = {0};
    frame.can_id = ((msg.type & 0x3F) << 5) | (dst_id & 0x1F);
    frame.can_dlc = sizeof(msg.payload);

    memcpy(frame.data, msg.payload, sizeof(msg.payload));

    auto ret = write(_can_socket_handle, &frame, sizeof(frame));
    if (ret < 0) {
        spdlog::error("Failed to send CAN message: {}", errno);
        return -errno;
    }

    return 0;
}

int JerryCAN::ReceiveMessage(jerrycan_msg_t &msg) {
    // Receive a message
    struct can_frame frame = {0};
    auto ret = read(_can_socket_handle, &frame, sizeof(frame));
    if (ret <= 0) {
        if (errno != EAGAIN) {
            spdlog::error("Failed to receive CAN message: {}", errno);
        }
        return -errno;
    }

    msg.type = static_cast<jerrycan_cmd_type_t>((frame.can_id >> 5) & 0x3F);
    msg.dst_id = frame.can_id & 0x1F;
    memcpy(msg.payload, frame.data, sizeof(msg.payload));

    return 0;
}

int JerryCAN::Heartbeat() {
    // Send a heartbeat message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_HEARTBEAT,
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

int JerryCAN::StepperMove(uint8_t dst_id, uint8_t stepper_id, uint16_t position, uint16_t max_velocity,
                          uint16_t max_acceleration, bool abs_or_rel) {
    // Send a stepper move message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_STEPPER_MOVE,
        .stepper_move =
            {
                .motor_id = stepper_id,
                .abs_or_rel = abs_or_rel,
                .position = position,
                .max_velocity = max_velocity,
                .max_acceleration = max_acceleration,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::ServoMove(uint8_t dst_id, uint8_t servo_id, uint16_t position, uint16_t max_velocity,
                        uint16_t max_acceleration, bool abs_or_rel) {
    // Send a servo move message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_SERVO_MOVE,
        .servo_move =
            {
                .motor_id = servo_id,
                .abs_or_rel = abs_or_rel,
                .position = position,
                .max_velocity = max_velocity,
                .max_acceleration = max_acceleration,
            },
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::StepperHome(uint8_t dst_id, uint8_t stepper_id) {
    // Send a stepper home message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_STEPPER_HOME,
        // FIXME: FILL THIS IN!
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

int JerryCAN::GPIOWrite(uint8_t dst_id, jerrycan_cmd_gpio_write_t &gpio_write) {
    // Send a GPIO Write Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_GPIO_WRITE,
        .gpio_write = gpio_write,
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::ToneWrite(uint8_t dst_id, jerrycan_cmd_tone_t &tone) {
    // Send a GPIO Write Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_TONE,
        .tone = tone,
    };

    return SendMessage(msg, dst_id);
}

int JerryCAN::AnalogOutWrite(uint8_t dst_id, jerrycan_cmd_analog_out_t &analog_out) {
    // Send an Analog Out Message
    jerrycan_msg_t msg = {
        .type = JERRYCAN_CMD_ANALOG_OUT,
        .analog_out = analog_out,
    };

    return SendMessage(msg, dst_id);
}
