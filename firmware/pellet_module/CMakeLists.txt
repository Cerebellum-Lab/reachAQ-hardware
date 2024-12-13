cmake_minimum_required(VERSION 3.20.0)

# Generate a version file based on the git tag & hash at project configuration time
execute_process(
        COMMAND python3 ../gen_version.py
        OUTPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/VERSION
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pellet_module_firmware)

set(SRCS
        src/main.c
)

target_sources(app PRIVATE ${SRCS})
target_include_directories(app PRIVATE .)
