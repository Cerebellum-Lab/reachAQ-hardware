/**
 * @file bootloader.c
 * @brief JerryCAN Bootloader Message Handling
 *
 * This file provides functionality to manage firmware image updates via CAN messages
 * through the JerryCAN library.
 *
 * Key Functions:
 * - `jerrycan_bootloader_rx_command_handler()`: Processes CAN messages and dispatches to appropriate
 *    bootloader functions based on subcommand type.
 *
 * Dependencies:
 *  - MCUBoot: Provides the bootloader functionality for updating firmware images.
 *  -
 *
 * Usage:
 * This module is initialized automatically at startup using Zephyr’s SYS_INIT macro
 */

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include "app_version.h"
#include "jerrycan.h"

LOG_MODULE_DECLARE(jerrycan, CONFIG_LIB_JERRYCAN_LOG_LEVEL);

#define SLOT1_PARTITION_ID FIXED_PARTITION_ID(slot1_partition)

static struct {
    struct flash_img_context flash_img_ctx;
    bool bootloader_active;
} bootloader_ctx;

// Send a JERRYCAN_CMD_BOOTLOADER_RESPONSE message with the currently running firmware version and the version in Slot 1
static void jerrycan_bootloader_send_version() {
    // Get the version in Slot 1
    struct mcuboot_img_header header;
    boot_read_bank_header(SLOT1_PARTITION_ID, &header, sizeof(header));

    // Craft the response message with the running version and the slot1 version
    jerrycan_msg_t response = {
        .type = JERRYCAN_CMD_BOOTLOADER_RESPONSE,
        .bootloader_response = {.type = JERRYCAN_BOOTLOADER_SUBCMD_VERSION,
                                .version =
                                    {
                                        .running_version_major = APP_VERSION_MAJOR,
                                        .running_version_minor = APP_VERSION_MINOR,
                                        .running_version_patch = APP_PATCHLEVEL,
                                        .slot1_version_major = header.h.v1.sem_ver.major,
                                        .slot1_version_minor = header.h.v1.sem_ver.minor,
                                        .slot1_version_patch = header.h.v1.sem_ver.revision,
                                    }},
    };

    // Print out the version response information to the console logs
    LOG_INF("Running Version: %d.%d.%d", response.bootloader_response.version.running_version_major,
            response.bootloader_response.version.running_version_minor,
            response.bootloader_response.version.running_version_patch);
    LOG_INF("Slot 1 Version: %d.%d.%d", response.bootloader_response.version.slot1_version_major,
            response.bootloader_response.version.slot1_version_minor,
            response.bootloader_response.version.slot1_version_patch);

    jerrycan_tx(&response, K_NO_WAIT);
}

static int jerrycan_bootloader_flash_img_init() {
    LOG_INF("Flash Image Update Start");

    // Set up the flash image context
    int ret = flash_img_init_id(&bootloader_ctx.flash_img_ctx, SLOT1_PARTITION_ID);
    if (ret) {
        // If the flash image setup fails for any reason, send a NACK response and return
        LOG_ERR("Failed to initialize flash image context: %d", ret);
        return ret;
    }

    bootloader_ctx.bootloader_active = true;
    return 0;
}

static int jerrycan_bootloader_flash_end() {
    LOG_INF("Flash Image Update End");

    // Finalize the flash image by flushing any remaining data to the flash
    int ret = flash_img_buffered_write(&bootloader_ctx.flash_img_ctx, NULL, 0, true);
    if (ret) {
        LOG_ERR("Failed to finalize flash image: %d", ret);
        return ret;
    }

    // On next boot, attempt to use the new image
    ret = boot_request_upgrade(BOOT_UPGRADE_TEST);

    bootloader_ctx.bootloader_active = false;
    return ret;
}

static void jerrycan_bootloader_rx_command_handler(jerrycan_msg_t *msg) {
    int ret = -EINVAL;
    jerrycan_msg_t resp;
    resp.type = JERRYCAN_CMD_BOOTLOADER_RESPONSE;

    switch (msg->bootloader_command.type) {
        case JERRYCAN_BOOTLOADER_SUBCMD_VERSION:
            jerrycan_bootloader_send_version();
            return;  // The version command crafts a more specific response, so no need to send a generic (N)ACK
        case JERRYCAN_BOOTLOADER_SUBCMD_START:
            ret = jerrycan_bootloader_flash_img_init();
            break;
        case JERRYCAN_BOOTLOADER_SUBCMD_END:
            ret = jerrycan_bootloader_flash_end();
            break;
        case JERRYCAN_BOOTLOADER_SUBCMD_REBOOT:
            LOG_PANIC();  // Put the logging subsystem in panic mode to disable buffered logs
            LOG_INF("Bootloader Reboot");
            sys_reboot(SYS_REBOOT_COLD);  // Reboot the system - this should not return
            break;
        case JERRYCAN_BOOTLOADER_SUBCMD_FINALIZE:
            LOG_INF("Bootloader Finalize");
            ret = boot_write_img_confirmed();
            break;
        default:
            LOG_ERR("Unknown bootloader command: %d", msg->bootloader_command.type);
            break;
    }

    if (ret == 0) {
        resp.bootloader_response.type = JERRYCAN_BOOTLOADER_SUBCMD_ACK;
    } else {
        LOG_WRN("Bootloader command failed: %d", ret);
        resp.bootloader_response.type = JERRYCAN_BOOTLOADER_SUBCMD_NACK;
    }

    // With each (N)ACK, provide some useful information for the host side to verify things are progressing as expected
    resp.bootloader_response.status.active = bootloader_ctx.bootloader_active;
    resp.bootloader_response.status.bytes_written = flash_img_bytes_written(&bootloader_ctx.flash_img_ctx);
    jerrycan_tx(&resp, K_NO_WAIT);
}

static void jerrycan_bootloader_rx_data_handler(jerrycan_msg_t *msg) {
    jerrycan_msg_t resp;
    resp.type = JERRYCAN_CMD_BOOTLOADER_RESPONSE;

    // If the START command hasn't been issued yet, don't accept data.  Respond with a NACK
    if (!bootloader_ctx.bootloader_active) {
        LOG_WRN("Received data before START command");
        resp.bootloader_response.type = JERRYCAN_BOOTLOADER_SUBCMD_NACK;
        jerrycan_tx(&resp, K_NO_WAIT);
        return;
    }

    // Write the data to the flash image
    int ret = flash_img_buffered_write(&bootloader_ctx.flash_img_ctx, msg->bootloader_data.data,
                                       JERRYCAN_MAX_PAYLOAD_SIZE, false);
    if (ret) {
        LOG_ERR("Failed to write image data: %d", ret);
        resp.bootloader_response.type = JERRYCAN_BOOTLOADER_SUBCMD_NACK;
        jerrycan_tx(&resp, K_NO_WAIT);
        return;
    }

    // Send an ACK response to indicate that the data was written successfully
    resp.bootloader_response.type = JERRYCAN_BOOTLOADER_SUBCMD_ACK;
    jerrycan_tx(&resp, K_NO_WAIT);
}

static jerrycan_rx_callback_t bootloader_rx_command_callback = {
    .filter_msg_type = JERRYCAN_CMD_BOOTLOADER_COMMAND,
    .func = jerrycan_bootloader_rx_command_handler,
};

static jerrycan_rx_callback_t bootloader_rx_data_callback = {
    .filter_msg_type = JERRYCAN_CMD_BOOTLOADER_DATA,
    .func = jerrycan_bootloader_rx_data_handler,
};

static int jerrycan_bootloader_init() {
    // Register the bootloader message handlers
    jerrycan_register_rx_callback(&bootloader_rx_command_callback);
    jerrycan_register_rx_callback(&bootloader_rx_data_callback);

    // Initialize the bootloader context
    bootloader_ctx.bootloader_active = false;

    return 0;
}

SYS_INIT(jerrycan_bootloader_init, APPLICATION, CONFIG_LIB_JERRYCAN_INIT_PRIORITY);
