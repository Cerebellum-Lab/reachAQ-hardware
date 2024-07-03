cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(magnet_module_firmware)

set(SRCS
        src/main.c
)

target_sources(app PRIVATE ${SRCS})
target_include_directories(app PRIVATE .)
