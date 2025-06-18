#include <spdlog/spdlog.h>

#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <thread>

#include "libjerrycan.h"

#define VERSION "0.0.1"

namespace po = boost::program_options;

static const std::chrono::duration<int, std::milli> kTimeout(1000);

static int wait_for_bootloader_response(JerryCAN &jc, const uint8_t node_id, const jerrycan_bootloader_subcmd_t subcmd,
                                        jerrycan_msg_t &msg) {
    // Wait for the response to come back
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        if (std::chrono::steady_clock::now() - start > kTimeout) {
            spdlog::debug("Timeout waiting for {} response", static_cast<int>(subcmd));
            return -EAGAIN;
        }

        if (jc.ReceiveMessage(msg) == 0) {
            if (msg.type == JERRYCAN_CMD_BOOTLOADER_RESPONSE && msg.dst_id == node_id &&
                msg.bootloader_response.type == subcmd) {
                return 0;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

static int wait_for_bootloader_response(JerryCAN &jc, uint8_t node_id, const jerrycan_bootloader_subcmd_t subcmd) {
    jerrycan_msg_t msg;
    return wait_for_bootloader_response(jc, node_id, subcmd, msg);
}

static int fetch_firmware_version(JerryCAN &jc, uint8_t node_id) {
    // Fetch Version information from the device
    if (int rc; (rc = jc.BootloaderCommand(node_id, JERRYCAN_BOOTLOADER_SUBCMD_VERSION)) == -1) {
      return rc;
    }

    // Wait for the response to come back
    jerrycan_msg_t msg;
    if (wait_for_bootloader_response(jc, node_id, JERRYCAN_BOOTLOADER_SUBCMD_VERSION, msg) == 0) {
        spdlog::info("Module {} Firmware Version: ", node_id);
        auto running_major = msg.bootloader_response.version.running_version_major;
        auto running_minor = msg.bootloader_response.version.running_version_minor;
        auto running_patch = msg.bootloader_response.version.running_version_patch;
        auto slot1_major = msg.bootloader_response.version.slot1_version_major;
        auto slot1_minor = msg.bootloader_response.version.slot1_version_minor;
        auto slot1_patch = msg.bootloader_response.version.slot1_version_patch;

        spdlog::info("\tRunning: {}.{}.{}", running_major, running_minor, running_patch);
        spdlog::info("\t  Slot1: {}.{}.{}", slot1_major, slot1_minor, slot1_patch);
    } else {
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    po::options_description desc("Allowed options");
    // clang-format off
  desc.add_options()
        ("help,h", "produce help message")
        ("version,V", "print version string")
        ("verbose,v", "enable verbose output")
        ("module,m", po::value<int>(), "Module address to update")
        ("file,f", po::value<std::string>(), "File to update with")
        ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("verbose")) {
        spdlog::set_level(spdlog::level::debug);
    }

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 1;
    }

    if (vm.count("version")) {
        std::cout << "JerryCAN Updater Version: " << VERSION << "\n";
        return 0;
    }

    if (vm.count("module") != 1) {
        spdlog::error("Please specify a module to update\n");
        return 1;
    }

    uint8_t node_id = vm["module"].as<int>();

    spdlog::info("Starting JerryCAN updater");

    auto jc = JerryCAN();
    jc.Open();

    auto ret = fetch_firmware_version(jc, node_id);
    if (ret != 0) {
        spdlog::error("Failed to fetch firmware version from module {}", node_id);
        return 1;
    }

    // If a file was specified, perform the update
    if (vm.count("file")) {
        spdlog::info("Updating module with file: {}", vm["file"].as<std::string>());

        // Open the file for sending
        std::ifstream firmware_binary(vm["file"].as<std::string>(), std::ios::binary | std::ios::in);
        if (!firmware_binary.is_open()) {
            spdlog::error("Failed to open firmware file: {}", vm["file"].as<std::string>());
            return 1;
        }

        // Initialize the flash image update
        spdlog::debug("Initializing flash image update");
        if (jc.BootloaderCommand(node_id, JERRYCAN_BOOTLOADER_SUBCMD_START) != 0) {
          return 1;
        }

        // Wait for an ACK
        spdlog::debug("Waiting for bootloader start ACK");
        ret = wait_for_bootloader_response(jc, node_id, JERRYCAN_BOOTLOADER_SUBCMD_ACK);
        if (ret != 0) {
            spdlog::error("Failed to start bootloader update");
            return 1;
        }
        spdlog::debug("Received bootloader start ACK");

        // Start sending data
        spdlog::info("Sending firmware image to module");
        while (firmware_binary.good()) {
            static jerrycan_cmd_bootloader_data_t data;

            // Read in bytes from the firmware file and send them to the module
            firmware_binary.read(reinterpret_cast<char*>(data.data), sizeof(data.data));

            if (jc.BootloaderData(node_id, data) != 0) {
              return 1;
            }

            ret = wait_for_bootloader_response(jc, node_id, JERRYCAN_BOOTLOADER_SUBCMD_ACK);
            if (ret != 0) {
                spdlog::error("Failed to send data to module");
                return 1;
            }
        }

        // End the flash image update
        spdlog::info("Ending flash image update");
        if (jc.BootloaderCommand(node_id, JERRYCAN_BOOTLOADER_SUBCMD_END) != 0) {
          return 1;
        }

        ret = wait_for_bootloader_response(jc, node_id, JERRYCAN_BOOTLOADER_SUBCMD_ACK);
        if (ret != 0) {
            spdlog::error("Failed to end bootloader update");
            return 1;
        }

        spdlog::info("Rebooting module");
        if (jc.BootloaderCommand(node_id, JERRYCAN_BOOTLOADER_SUBCMD_REBOOT) != 0) {
          return 1;
        }

        // Wait a few seconds for the module to reboot and then attempt to read the version information again
        std::this_thread::sleep_for(std::chrono::seconds(10));

        for (auto i = 10; i >= 0; i--) {
            spdlog::debug("Waiting for module to reboot: {}", i);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            ret = fetch_firmware_version(jc, node_id);
            if (ret == 0) {
                break;
            }
        }

        // Finalize the update if the version information was successfully fetched - that indicates that the firmware
        // booted and the CAN interface at least is still functional
        if (jc.BootloaderCommand(node_id, JERRYCAN_BOOTLOADER_SUBCMD_FINALIZE) != 0) {
          return 1;
        }

        ret = wait_for_bootloader_response(jc, node_id, JERRYCAN_BOOTLOADER_SUBCMD_ACK);
        if (ret != 0) {
            spdlog::error("Failed to finalize bootloader update");
            return 1;
        }

        spdlog::info("JerryCAN update complete");

        return 0;
    }
}
