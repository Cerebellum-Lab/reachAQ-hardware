/**
 * @file capabilities.c
 * @brief Answer JERRYCAN_CMD_CAPABILITIES_REQUEST with what this board can do.
 *
 * The host has asked this question since before any firmware could answer it.
 * Until now every board stayed silent, so reachAQ had to infer a board's
 * abilities from its version string against a table it maintains by hand -
 * which means a board that gains a feature needs a host release to notice, and
 * a board that lacks one is only caught when a command fails in the middle of
 * an experiment.
 *
 * A bit is set here only where the feature is genuinely present on this board.
 * The finite pulse bit is derived from the same devicetree condition gpio.c
 * uses to decide whether it can serve a pulse at all, so the two cannot
 * disagree: a board without the counter reports no pulse and refuses the
 * command, and a board with it reports and serves. Advertising a capability
 * that is absent would be worse than the silence it replaces, because the host
 * stops guarding against what a board claims to have.
 *
 * Two bits the host defines are deliberately never set:
 *
 *   - TIMING_TRAILER, because nothing in this firmware appends one, and
 *     nothing in the host reads one. It is a name reserved on both sides.
 *   - TIME_SYNC, because JERRYCAN_CMD_TIME_SYNC_REQUEST (0x1F) has no handler
 *     in this firmware and no entry in the command enum. A board that claimed
 *     it would be asked for a sync it cannot perform.
 *
 * Dependencies:
 * - `jerrycan_register_rx_callback()`: registers the handler.
 * - `jerrycan_tx()`: transmits the response.
 */

#include <jerrycan.h>
#include <jerrycan_types.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

/*
    The same condition gpio.c gates JERRYCAN_CMD_GPIO_PULSE on. Written here as
    a separate expression rather than shared through a header because it is a
    property of the board, and a board that grows a counter should start
    reporting the bit without either file being edited.
*/
#define CAPABILITY_FINITE_GPIO_PULSE                                                                               \
    (DT_HAS_CHOSEN(ll_gpio_pulse_counter) ? JERRYCAN_CAPABILITY_FINITE_GPIO_PULSE : 0U)

#define BOARD_CAPABILITIES (CAPABILITY_FINITE_GPIO_PULSE)

/*
    Drawn once at init rather than per request, so every answer within one boot
    carries the same value and a change of value means a reboot and nothing
    else.
*/
static uint32_t boot_id;

static int capabilities_request(const jerrycan_msg_t *msg) {
    jerrycan_msg_t response = {
        .type = JERRYCAN_CMD_CAPABILITIES_RESPONSE,
        .capabilities_response =
            {
                .wire_schema_version = JERRYCAN_WIRE_SCHEMA_VERSION,
                .capabilities = BOARD_CAPABILITIES,
                .boot_id = boot_id,
            },
        .uuid = msg->uuid,
    };

    const int ret = jerrycan_tx(&response, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("Failed to answer the capabilities request - %d", ret);
        return ret;
    }

    return 0;
}

static jerrycan_rx_callback_t capabilities_callback = {
    .filter_msg_type = JERRYCAN_CMD_CAPABILITIES_REQUEST,
    .func = capabilities_request,
};

static int jerrycan_capabilities_init() {
    boot_id = sys_rand32_get();
    jerrycan_register_rx_callback(&capabilities_callback);
    LOG_INF("Capabilities 0x%08x, wire schema %u, boot id 0x%08x", BOARD_CAPABILITIES,
            JERRYCAN_WIRE_SCHEMA_VERSION, boot_id);
    return 0;
}

SYS_INIT(jerrycan_capabilities_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
