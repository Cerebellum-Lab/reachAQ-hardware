#include "jerrycan.h"

#include <generic_gpios.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct device *gpio_dev = DEVICE_DT_GET_ANY(ll_generic_gpios);

LOG_MODULE_REGISTER(jerrycan, LOG_LEVEL_DBG);

CAN_MSGQ_DEFINE(jerrycan_msgq, 10);

// CAN_ID[10:0] = { MsgID[5:0], DeviceType[2:0], ModuleAddr[1:0] }
// The combination of DeviceType and ModuleAddr uniquely identifies this device (NODE_ID)
#define NODE_ID_MASK 0x1F
static uint8_t can_node_id;

static inline uint16_t can_id(jerrycan_cmd_type_t msg_type) { return (uint16_t)msg_type << 5 | can_node_id; }

int jerrycan_rx_poll(jerrycan_msg_t *msg, k_timeout_t timeout) {
    struct can_frame frame;

    // Get a CAN frame off of the message queue
    int ret = k_msgq_get(&jerrycan_msgq, &frame, timeout);
    if (ret) {
        return ret;
    }

    // Parse the CAN_ID from the frame into a message type
    msg->type = frame.id >> 5;

    if (frame.dlc > 8) {
        LOG_WRN("Received CAN frame with DLC > 8: %d", frame.dlc);
    }

    // Copy the data from the CAN frame into the message payload
    uint8_t msg_len = MIN(frame.dlc, sizeof(msg->payload));
    memset(msg->payload, 0, sizeof(msg->payload));
    memcpy(msg->payload, frame.data, msg_len);

    return 0;
}

static uint8_t get_can_node_id() {
    // Read the DeviceType bits to make sure this is a Pellet Module
    uint32_t device_type;
    ll_generic_gpio_read(gpio_dev, 0x3, &device_type);

    // Read the NodeID to program up the CAN_ID properly
    uint32_t node_id;
    ll_generic_gpio_read(gpio_dev, 0xC, &node_id);
    node_id = (node_id >> 2) & 0x3;

    // Print out the device type and node ID
    LOG_INF("CAN_ID: 0x%X", (node_id << 2) | device_type);

    return ((node_id << 2) | device_type) & NODE_ID_MASK;
}

int jerrycan_tx(jerrycan_msg_t *msg) {
    struct can_frame frame = {
        .id = can_id(msg->type),
        .dlc = sizeof(msg->payload),
    };

    memcpy(frame.data, msg->payload, sizeof(msg->payload));

    return can_send(can_dev, &frame, K_FOREVER, NULL, NULL);
}

static int jerrycan_set_bitrates(uint32_t bitrate, uint32_t data_bitrate) {
    int ret;
    uint32_t can_core_clock;

    // Report the min and max supported data bitrates
    uint32_t max_bitrate = can_get_bitrate_max(can_dev);
    uint32_t min_bitrate = can_get_bitrate_min(can_dev);
    can_get_core_clock(can_dev, &can_core_clock);
    LOG_INF("CAN Bitrate: %d - %d :: %d", min_bitrate, max_bitrate, can_core_clock);

    // Set up the bitrate for standard CAN messages
    struct can_timing can_timing;
    ret = can_calc_timing(can_dev, &can_timing, bitrate, 875);
    if (ret < 0) {
        LOG_ERR("Failed to calculate CAN timing: %d", ret);
        return ret;
    }

    ret = can_set_timing(can_dev, &can_timing);
    if (ret < 0) {
        LOG_ERR("Failed to set CAN timing: %d", ret);
        return ret;
    }

    // Set up the bitrate for FD CAN data
    ret = can_calc_timing_data(can_dev, &can_timing, data_bitrate, 875);
    if (ret < 0) {
        LOG_ERR("Failed to calculate CAN data timing: %d", ret);
        return ret;
    }

    ret = can_set_timing_data(can_dev, &can_timing);
    if (ret < 0) {
        LOG_ERR("Failed to set CAN data timing: %d", ret);
        return ret;
    }

    return 0;
}

static int jerrycan_init() {
    int ret;

    // Read this device type and address from GPIOS
    can_node_id = get_can_node_id();

    // Ensure the CAN device is ready
    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return -ENODEV;
    }

    // Set up the bitrate for the CAN device to be 250 kbps / 5 Mbps
    ret = jerrycan_set_bitrates(250000, 5000000);
    if (ret) {
        LOG_ERR("Failed to set CAN bitrates: %d", ret);
        return ret;
    }

    ret = can_start(can_dev);
    if (ret) {
        LOG_ERR("Failed to start CAN device: %d", ret);
        return ret;
    }

    // Add the filter for messages address to this specific device
    const struct can_filter jerrycan_rx_filter = {
        .flags = 0,
        .id = can_node_id,
        .mask = NODE_ID_MASK,
    };

    ret = can_add_rx_filter_msgq(can_dev, &jerrycan_msgq, &jerrycan_rx_filter);
    if (ret < 0) {
        LOG_ERR("Failed to add CAN filter: %d", ret);
        return ret;
    }

    // Add the filter for broadcast messages (NodeID = 0x1F)
    const struct can_filter jerrycan_broadcast_filter = {
        .flags = 0,
        .id = NODE_ID_MASK,
        .mask = NODE_ID_MASK,
    };

    ret = can_add_rx_filter_msgq(can_dev, &jerrycan_msgq, &jerrycan_broadcast_filter);
    if (ret < 0) {
        LOG_ERR("Failed to add CAN filter: %d", ret);
        return ret;
    }

    return 0;
}

SYS_INIT(jerrycan_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);