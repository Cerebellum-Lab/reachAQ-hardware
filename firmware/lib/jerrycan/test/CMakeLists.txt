# Host-run tests for the parts of JerryCAN that do not depend on Zephyr.
# Zephyr's native_sim cannot build in this workspace (drivers/motor/motor_common.c
# needs STM32 LL headers), so these compile with the host C compiler instead.
cmake_minimum_required(VERSION 3.20)

project(jerrycan_host_test C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wextra -Werror")

add_executable(timing_trailer_test timing_trailer_test.c ../timing_trailer.c)

target_include_directories(timing_trailer_test
    PRIVATE
        ..
        ../../../../software/libjerrycan/inc
)

add_test(NAME timing_trailer_test COMMAND timing_trailer_test)

add_executable(board_clock_test board_clock_test.c)

target_include_directories(board_clock_test
    PRIVATE
        ../../board_clock
)

add_test(NAME board_clock_test COMMAND board_clock_test)
