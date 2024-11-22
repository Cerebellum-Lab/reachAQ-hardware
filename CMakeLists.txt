cmake_minimum_required(VERSION 3.20)

project(MouseGym)

add_subdirectory(software)

# Use west to build the firmware images
set(MAGNET_MODULE_BIN_PATH ${CMAKE_SOURCE_DIR}/firmware/magnet_module/build/magnet_module/zephyr/zephyr.signed.bin)
add_custom_command(
        COMMAND west build --sysbuild -b cerebellumlab_magnet_module -p
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/firmware/magnet_module
        OUTPUT ${MAGNET_MODULE_BIN_PATH}
)

install(
        FILES ${MAGNET_MODULE_BIN_PATH}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/firmware
        RENAME magnet_module.bin
)
add_custom_target(
        magnet_module_firmware
        DEPENDS ${MAGNET_MODULE_BIN_PATH}
)

set(PELLET_MODULE_BIN_PATH ${CMAKE_SOURCE_DIR}/firmware/pellet_module/build/pellet_module/zephyr/zephyr.signed.bin)
add_custom_command(
        COMMAND west build --sysbuild -b cerebellumlab_pellet_module -p
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/firmware/pellet_module
        OUTPUT ${PELLET_MODULE_BIN_PATH}
)
add_custom_target(
        pellet_module_firmware
        DEPENDS ${PELLET_MODULE_BIN_PATH}
)

install(
        FILES ${PELLET_MODULE_BIN_PATH}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/firmware
        RENAME pellet_module.bin
)
