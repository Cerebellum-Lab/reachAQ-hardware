#include "jerrycan.h"

#include <generic_gpios.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>
#include <zephyr/random/random.h>

#include "board_clock.h"

#if CONFIG_LIB_JERRYCAN_TIMING
#include "timing_trailer.h"
#endif

static const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct device *gpio_dev = DEVICE_DT_GET_ANY(ll_generic_gpios);

LOG_MODULE_REGISTER(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

static sys_slist_t can_rx_callbacks_list;

/*
    Every received frame is stamped in the CAN RX interrupt, before the jerrycan thread
    reaches it, so a TIME_SYNC request or a clap is timed when it arrived rather than
    when the thread got round to it. One callback serves every filter: the FDCAN hands
    a frame only to the first filter it matches, so a separate "timing" filter would
    silently lose to the node filter.
*/
typedef struct {
    uint64_t rx_time_us;
    struct can_frame frame;
} jerrycan_rx_item_t;

/*
    A queued message and what its timing trailer reports. Without timing there is no
    trailer, so the item is the bare message and the queue costs what it did in v2.3.0.
*/
typedef struct {
#if CONFIG_LIB_JERRYCAN_TIMING
    uint64_t board_time_us;
    uint8_t kind;
    uint8_t flags;
#endif
    jerrycan_msg_t msg;
} jerrycan_tx_item_t;

K_MSGQ_DEFINE(jerrycan_rx_msgq, sizeof(jerrycan_rx_item_t), 10, 8);
#if CONFIG_LIB_JERRYCAN_TIMING
K_MSGQ_DEFINE(jerrycan_tx_msgq, sizeof(jerrycan_tx_item_t), 150, 8);
#else
BUILD_ASSERT(sizeof(jerrycan_tx_item_t) == sizeof(jerrycan_msg_t), "Without timing a TX queue item is the bare message");
K_MSGQ_DEFINE(jerrycan_tx_msgq, sizeof(jerrycan_tx_item_t), 150, 4);
#endif

static uint32_t boot_id;
static uint64_t current_rx_time_us;

#if CONFIG_LIB_JERRYCAN_TIMING
/* Messages jerrycan_tx_timed() could not queue. jerrycan_run() folds them into the trailer sequence. */
static atomic_t tx_dropped;
#endif

/*
    Board time is the board clock (board_clock.h) in whole microseconds. The conversion is one exact divide, which is
    what the CAN RX interrupt should pay; board_clock.c asserts the whole-MHz core clock that makes it exact.
*/
uint64_t jerrycan_cycles_to_board_time_us(uint64_t cycles) { return board_clock_cycles_to_us(cycles); }

uint64_t jerrycan_board_time_us(void) { return jerrycan_cycles_to_board_time_us(board_clock_cycles()); }

uint32_t jerrycan_boot_id(void) { return boot_id; }

uint64_t jerrycan_rx_time_us(void) { return current_rx_time_us; }

static void jerrycan_rx_isr(const struct device *dev, struct can_frame *frame, void *user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    /* Stamp first: the copy below is not part of the arrival time */
    const uint64_t rx_time_us = jerrycan_board_time_us();
    const jerrycan_rx_item_t item = {.rx_time_us = rx_time_us, .frame = *frame};

    if (k_msgq_put(&jerrycan_rx_msgq, &item, K_NO_WAIT) != 0) {
        LOG_ERR("JerryCAN RX queue overflowed. Frame ID: 0x%x", frame->id);
    }
}

// CAN_ID[10:0] = { MsgID[5:0], DeviceType[2:0], ModuleAddr[1:0] }
// The combination of DeviceType and ModuleAddr uniquely identifies this device (NODE_ID)
#define NODE_ID_MASK 0x1F
static uint8_t can_node_id;

static void (*host_frame_hook)(void);

// Message types that a board sends and every other board hears, so an RX filter admits them from any address and the
// low five id bits name the sender instead of the destination. They are not host frames: the host-frame hook must never
// see one, because a sender at 0x1F or at this board's own address would pass its address test. Every type that
// an any-address filter in jerrycan_init() admits must be listed here.
static bool is_board_frame_type(uint8_t type) { return type == JERRYCAN_CMD_SYNC_CLAP; }

static inline uint16_t can_id(jerrycan_cmd_type_t msg_type) { return (uint16_t)msg_type << 5 | can_node_id; }

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

// Return the payload size for a given message type
static uint8_t jerrycan_msg_get_payload_size(jerrycan_cmd_type_t msg_type) {
    static const uint8_t jerrycan_size_map[] = {
        [JERRYCAN_CMD_ESTOP] = sizeof(jerrycan_cmd_estop_t),
        [JERRYCAN_CMD_HEARTBEAT] = sizeof(jerrycan_cmd_heartbeat_t),
        [JERRYCAN_CMD_STATUS] = sizeof(jerrycan_cmd_status_t),
        [JERRYCAN_CMD_STEPPER_MOVE] = sizeof(jerrycan_cmd_stepper_move_t),
        [JERRYCAN_CMD_SERVO_MOVE] = sizeof(jerrycan_cmd_servo_move_t),
        [JERRYCAN_CMD_SERVO_ATTACH] = sizeof(jerrycan_cmd_servo_attach_t),
        [JERRYCAN_CMD_SERVO_DETACH] = sizeof(jerrycan_cmd_servo_detach_t),
        [JERRYCAN_CMD_STEPPER_HOME] = sizeof(jerrycan_cmd_stepper_home_t),
        [JERRYCAN_CMD_CFG_WRITE] = sizeof(jerrycan_cmd_cfg_t),
        [JERRYCAN_CMD_CFG_RESPONSE] = sizeof(jerrycan_cmd_cfg_t),
        [JERRYCAN_CMD_CFG_READ] = sizeof(jerrycan_cmd_cfg_t),
        [JERRYCAN_CMD_STEPPER_STATUS] = sizeof(jerrycan_cmd_stepper_status_t),
        [JERRYCAN_CMD_SERVO_STATUS] = sizeof(jerrycan_cmd_servo_status_t),
        [JERRYCAN_CMD_PRESSURE_READ] = sizeof(jerrycan_cmd_pressure_read_t),
        [JERRYCAN_CMD_TEMP_HUM_READ] = sizeof(jerrycan_cmd_temp_hum_read_t),
        [JERRYCAN_CMD_GPIO_READ] = sizeof(jerrycan_cmd_gpio_read_t),
        [JERRYCAN_CMD_GPIO_WRITE] = sizeof(jerrycan_cmd_gpio_write_t),
        [JERRYCAN_CMD_GPIO_PULSE] = sizeof(jerrycan_cmd_gpio_pulse_t),
        [JERRYCAN_CMD_GPIO_PULSE_STATUS] = sizeof(jerrycan_cmd_gpio_pulse_status_t),
        [JERRYCAN_CMD_CAPABILITIES_REQUEST] = sizeof(jerrycan_cmd_capabilities_request_t),
        [JERRYCAN_CMD_CAPABILITIES_RESPONSE] = sizeof(jerrycan_cmd_capabilities_response_t),
        [JERRYCAN_CMD_TIME_SYNC_REQUEST] = sizeof(jerrycan_cmd_time_sync_request_t),
        [JERRYCAN_CMD_TIME_SYNC_RESPONSE] = sizeof(jerrycan_cmd_time_sync_response_t),
        [JERRYCAN_CMD_SYNC_CLAP] = sizeof(jerrycan_cmd_sync_clap_t),
        [JERRYCAN_CMD_SYNC_CLAP_OBSERVED] = sizeof(jerrycan_cmd_sync_clap_observed_t),
        [JERRYCAN_CMD_SYNC_BARCODE] = sizeof(jerrycan_cmd_sync_barcode_t),
        [JERRYCAN_CMD_ANALOG_BLOCK] = sizeof(jerrycan_cmd_analog_block_t),
        [JERRYCAN_CMD_PWM_SET] = sizeof(jerrycan_cmd_pwm_set_t),
        [JERRYCAN_CMD_TONE] = sizeof(jerrycan_cmd_tone_t),
        [JERRYCAN_CMD_ANALOG_OUT] = sizeof(jerrycan_cmd_analog_out_t),
        [JERRYCAN_CMD_LOAD_CELL_READ] = sizeof(jerrycan_cmd_load_cell_read_t),
        [JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_BEGIN] = sizeof(jerrycan_cmd_audio_data_cmd_t),
        [JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_CONT] = sizeof(jerrycan_cmd_audio_data_t),
        [JERRYCAN_CMD_AUDIO_MAGNITUDE_DATA_END] = sizeof(jerrycan_cmd_audio_data_cmd_t),
        [JERRYCAN_CMD_LOAD_CELL_TARE] = sizeof(jerrycan_cmd_load_cell_tare_t),
        [JERRYCAN_CMD_RGB_LED] = sizeof(jerrycan_cmd_rgb_led_t),
        [JERRYCAN_CMD_DOOR_SENSOR] = sizeof(jerrycan_cmd_door_closed_t),
        [JERRYCAN_CMD_BOOTLOADER_COMMAND] = sizeof(jerrycan_cmd_bootloader_command_t),
        [JERRYCAN_CMD_BOOTLOADER_RESPONSE] = sizeof(jerrycan_cmd_bootloader_response_t),
        [JERRYCAN_CMD_BOOTLOADER_DATA] = sizeof(jerrycan_cmd_bootloader_data_t),
        [JERRYCAN_CMD_DELAY] = sizeof(jerrycan_cmd_delay_t),
        [JERRYCAN_CMD_FIXED_XYZ] = sizeof(jerrycan_cmd_fixed_xyz),
        [JERRYCAN_RSP_ACK] = sizeof(jerrycan_rsp_ack_t),
    };

    if (msg_type > JERRYCAN_CMD_MAX || msg_type < JERRYCAN_CMD_MIN) {
        return 0;
    }

    return jerrycan_size_map[msg_type];
}

// Add a jerrycan message to the TX queue- these messages will be processed by the main jerrycan_run() loop
// This allows any thread or interrupt to generate an outgoing CAN message which will then be handled by the main task
int jerrycan_tx(jerrycan_msg_t *msg, k_timeout_t timeout) {
#if CONFIG_LIB_JERRYCAN_TIMING
    return jerrycan_tx_timed(msg, JERRYCAN_TIMESTAMP_TX_QUEUE, 0, jerrycan_board_time_us(), timeout);
#else
    // No trailer to report a time in, so the clock is not read
    return jerrycan_tx_timed(msg, JERRYCAN_TIMESTAMP_TX_QUEUE, 0, 0, timeout);
#endif
}

int jerrycan_tx_timed(jerrycan_msg_t *msg, jerrycan_timestamp_kind_t kind, uint8_t flags, uint64_t board_time_us,
                      k_timeout_t timeout) {
    static size_t failureCount = 0;

#if CONFIG_LIB_JERRYCAN_TIMING
    const jerrycan_tx_item_t item = {
        .board_time_us = board_time_us,
        .kind = (uint8_t)kind,
        .flags = flags,
        .msg = *msg,
    };
#else
    // Without the trailer there is nowhere to report these
    ARG_UNUSED(kind);
    ARG_UNUSED(flags);
    ARG_UNUSED(board_time_us);

    const jerrycan_tx_item_t item = {.msg = *msg};
#endif

    // Place message in queue
    const int ret = k_msgq_put(&jerrycan_tx_msgq, &item, timeout);
    if (ret != 0) {
#if CONFIG_LIB_JERRYCAN_TIMING
        atomic_inc(&tx_dropped);
#endif
        if (!failureCount++) {
            LOG_ERR("jerrycan_tx failed to place message in queue: %d", ret);
        }
    } else {
        if (failureCount) {
            LOG_ERR("jerrycan transmissions restored. %d lost packets.", failureCount);
        }
        failureCount = 0;
    }

    return ret;
}

// Consolidated function for TX and RX handling
int jerrycan_run(k_timeout_t timeout) {
    static jerrycan_msg_t msg;
    static struct can_frame frame;
    static jerrycan_rx_item_t rx_item;
    static jerrycan_tx_item_t tx_item;
#if CONFIG_LIB_JERRYCAN_TIMING
    /*
        Counts trailer-bearing frames and messages dropped because the TX queue was full,
        so the host can tell a lost frame from a quiet board, including one lost before
        it was queued.
    */
    static uint32_t trailer_sequence;
#endif
    int ret;

    void confirm_image();
    confirm_image();

    static struct k_poll_event events[2] = {
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &jerrycan_tx_msgq, 1),
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &jerrycan_rx_msgq,
                                        2)};

    // Poll for events in the RX and TX queues
    ret = k_poll(events, sizeof(events) / sizeof(struct k_poll_event), timeout);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            LOG_DBG("JerryCAN timed out while polling for events");
        } else {
            LOG_ERR("JerryCAN failed to poll for events: %d", ret);
        }
        return ret;
    }

    // TX processing
    if (events[0].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
        while (k_msgq_get(&jerrycan_tx_msgq, &tx_item, K_NO_WAIT) == 0) {
            msg = tx_item.msg;
            uint8_t payload_size = jerrycan_msg_get_payload_size(msg.type);
            frame.id = can_id(msg.type);

            // Enable CAN FD frames and Bit Rate Switching
            frame.flags = CAN_FRAME_FDF | CAN_FRAME_BRS;

            // Copy the payload into the CAN frame
            memcpy(frame.data, msg.payload, payload_size);
            if (payload_size < sizeof(msg.payload)) {
                memcpy(frame.data + payload_size, &msg.uuid, sizeof(msg.uuid));
                payload_size += sizeof(msg.uuid);
            }

#if CONFIG_LIB_JERRYCAN_TIMING
            // Drops since the last dequeue leave a gap in the sequence, as a lost frame would
            trailer_sequence += (uint32_t)atomic_clear(&tx_dropped);

            const jerrycan_timing_t timing = {
                .kind = tx_item.kind,
                .flags = tx_item.flags,
                .boot_id = boot_id,
                .sequence = trailer_sequence,
                .board_time_us = tx_item.board_time_us,
            };
            const size_t with_trailer =
                jerrycan_timing_append_trailer(frame.data, payload_size, sizeof(frame.data), &timing);
            if (with_trailer != payload_size) {
                payload_size = (uint8_t)with_trailer;
                trailer_sequence++;
            }
#endif

            frame.dlc = can_bytes_to_dlc(payload_size);

            // For payload sizes > 8, DLC no longer maps 1:1 to the number of bytes in the payload
            // If the actual payload size is less than the number of bytes indicated by DLC, pad the rest with 0
            // const uint8_t dlc_bytes = can_dlc_to_bytes(frame.dlc);
            // memset(&frame.data[payload_size], 0, dlc_bytes - payload_size);

            ret = can_send(can_dev, &frame, K_FOREVER, NULL, NULL);
            if (ret != 0) {
                LOG_ERR("Failed to send CAN frame: %d", ret);
            }
        }
    }

    // RX processing
    if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
        while (k_msgq_get(&jerrycan_rx_msgq, &rx_item, K_NO_WAIT) == 0) {
            frame = rx_item.frame;
            current_rx_time_us = rx_item.rx_time_us;
            msg.type = frame.id >> 5;
            // Low 5 id bits: this board or 0x1F for host commands, the sender for board frames such as claps
            msg.dst_id = frame.id & NODE_ID_MASK;

            // Only host frames feed the host-silence timer. A board frame that a filter admits from any address, a
            // clap for one, may carry 0x1F or this board's own address, so the address alone cannot rule it out.
            if (host_frame_hook != NULL && !is_board_frame_type(msg.type) &&
                (msg.dst_id == can_node_id || msg.dst_id == NODE_ID_MASK)) {
                host_frame_hook();
            }

            uint8_t msg_len = jerrycan_msg_get_payload_size(msg.type);
            memcpy(msg.payload, frame.data, msg_len);
            if (msg_len < sizeof(msg.payload)) {
                memcpy(&msg.uuid, frame.data + msg_len, sizeof(msg.uuid));
            }

            sys_snode_t *snode;
            SYS_SLIST_FOR_EACH_NODE(&can_rx_callbacks_list, snode) {
                jerrycan_rx_callback_t *callback = CONTAINER_OF(snode, jerrycan_rx_callback_t, node);
                if (callback->filter_msg_type == msg.type) {
                    int error = callback->func(&msg);
                    if ((error != COMMAND_NOT_COMPLETE) && (error != SEND_NO_ACKNOWLEDGEMENT)) {
                        jerrycan_send_ack(msg.uuid, error);
                    }
                }
            }
        }
    }

    // Reset event states
    events[0].state = K_POLL_STATE_NOT_READY;
    events[1].state = K_POLL_STATE_NOT_READY;

    return ret;
}

void jerrycan_send_ack(const uint8_t uuid, const int error_code) {
    jerrycan_msg_t msg = {
        .type = JERRYCAN_RSP_ACK,
        .ack =
            {
                .error = error_code,
            },
        .uuid = uuid,
    };

    jerrycan_tx(&msg, K_NO_WAIT);
}

void jerrycan_register_rx_callback(jerrycan_rx_callback_t *callback) {
    sys_slist_append(&can_rx_callbacks_list, &callback->node);
}

void jerrycan_set_host_frame_hook(void (*hook)(void)) { host_frame_hook = hook; }

static int jerrycan_init() {
    int ret;

    // Initialize the linked list that will hold the callbacks to be called on RX frame
    sys_slist_init(&can_rx_callbacks_list);

    // Drawn before any module can queue a frame, so every trailer of this boot carries it
    boot_id = sys_rand32_get();

    // Read this device type and address from GPIOS
    can_node_id = get_can_node_id();

    // Ensure the CAN device is ready
    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return -ENODEV;
    }

    ret = can_set_mode(can_dev, CAN_MODE_FD);
    if (ret < 0) {
        LOG_ERR("Failed to set CAN bus to FD Mode");
        return ret;
    }

    // Set up the arbitration-phase bitrate for the CAN device to be 1 Mbps
    ret = can_set_bitrate(can_dev, 1000000);
    if (ret != 0) {
        LOG_ERR("Failed to set CAN bitrate: %d", ret);
        return ret;
    }

    // Set up the data-phase bitrate for the CAN device to be 5 Mbps
    ret = can_set_bitrate_data(can_dev, 5000000);
    if (ret != 0) {
        LOG_ERR("Failed to set CAN data bitrate: %d", ret);
        return ret;
    }

    // Start the CAN device
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

    ret = can_add_rx_filter(can_dev, jerrycan_rx_isr, NULL, &jerrycan_rx_filter);
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

    ret = can_add_rx_filter(can_dev, jerrycan_rx_isr, NULL, &jerrycan_broadcast_filter);
    if (ret < 0) {
        LOG_ERR("Failed to add CAN filter: %d", ret);
        return ret;
    }

#if CONFIG_LIB_JERRYCAN_TIMING
    // Claps are sent from the timekeeper's own address, so neither filter above sees them.
    // This filter admits a type from any address. Every type admitted like that must be listed in
    // is_board_frame_type(), or the host-silence hook counts it as host traffic when it comes from 0x1F or from this
    // board's own address.
    const struct can_filter jerrycan_clap_filter = {
        .flags = 0,
        .id = (uint32_t)JERRYCAN_CMD_SYNC_CLAP << 5,
        .mask = 0x3F << 5,
    };

    ret = can_add_rx_filter(can_dev, jerrycan_rx_isr, NULL, &jerrycan_clap_filter);
    if (ret < 0) {
        LOG_ERR("Failed to add CAN clap filter: %d", ret);
        return ret;
    }
#endif

    return 0;
}

SYS_INIT(jerrycan_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
