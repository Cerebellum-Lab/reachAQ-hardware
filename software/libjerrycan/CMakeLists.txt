cmake_minimum_required(VERSION 3.20)

project(jerrycan_host)

set(CMAKE_CXX_STANDARD 23)

include_directories(
        inc
)

add_subdirectory(src)
add_subdirectory(bindings)
