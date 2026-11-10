/**
 * @file timing.c
 * @brief Answer TIME_SYNC requests and report the sync claps this board hears.
 *
 * Both answers use times stamped in the CAN RX interrupt (jerrycan_rx_time_us()),
 * so however long the jerrycan thread takes to reach a frame does not enter them.
 *
 * - TIME_SYNC_REQUEST (0x1F): reachAQ's NTP-style exchange. The response says when
 *   the request arrived and when the response was queued. Its timing trailer
 *   carries boot_id, without which reachAQ ignores the response.
 * - SYNC_CLAP (0x25): the timekeeper's once-a-second clap, accepted from any
 *   address. Answered with SYNC_CLAP_OBSERVED, stamped OBSERVED at arrival.
 *
 * Neither is acknowledged: the time-sync response is the answer, and a clap is a
 * broadcast that nobody waits on.
 */

#include <jerrycan.h>
#include <jerrycan_types.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

static int time_sync_request(const jerrycan_msg_t *msg) {
    jerrycan_msg_t response = {
        .type = JERRYCAN_CMD_TIME_SYNC_RESPONSE,
        .time_sync_response =
            {
                .request_id = msg->time_sync_request.request_id,
                .request_receive_time_us = jerrycan_rx_time_us(),
            },
        .uuid = msg->uuid,
    };

    const uint64_t queued_us = jerrycan_board_time_us();
    response.time_sync_response.response_queue_time_us = queued_us;

    const int ret = jerrycan_tx_timed(&response, JERRYCAN_TIMESTAMP_TX_QUEUE, 0, queued_us, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("Failed to answer TIME_SYNC request %u - %d", msg->time_sync_request.request_id, ret);
    }

    return SEND_NO_ACKNOWLEDGEMENT;
}

static int sync_clap_heard(const jerrycan_msg_t *msg) {
    jerrycan_msg_t observed = {
        .type = JERRYCAN_CMD_SYNC_CLAP_OBSERVED,
        .sync_clap_observed =
            {
                .tick = msg->sync_clap.tick,
                .clapper_node = msg->dst_id,
            },
    };

    const int ret = jerrycan_tx_timed(&observed, JERRYCAN_TIMESTAMP_OBSERVED, 0, jerrycan_rx_time_us(), K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("Failed to report clap %u - %d", msg->sync_clap.tick, ret);
    }

    return SEND_NO_ACKNOWLEDGEMENT;
}

static jerrycan_rx_callback_t time_sync_callback = {
    .filter_msg_type = JERRYCAN_CMD_TIME_SYNC_REQUEST,
    .func = time_sync_request,
};

static jerrycan_rx_callback_t sync_clap_callback = {
    .filter_msg_type = JERRYCAN_CMD_SYNC_CLAP,
    .func = sync_clap_heard,
};

static int jerrycan_timing_init() {
    jerrycan_register_rx_callback(&time_sync_callback);
    jerrycan_register_rx_callback(&sync_clap_callback);
    return 0;
}

SYS_INIT(jerrycan_timing_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
