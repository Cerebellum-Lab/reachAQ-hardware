cmake_minimum_required(VERSION 3.20)

project(jerrycan_host)

set(CMAKE_CXX_STANDARD 23)

include_directories(
        inc
)

add_subdirectory(bindings)

include(FetchContent)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.x
)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

FetchContent_MakeAvailable(spdlog)

add_library(jerrycan STATIC
        src/libjerrycan.cpp
)

target_sources(jerrycan PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS inc
        FILES
            inc/libjerrycan.h
            inc/jerrycan_types.h
)

target_link_libraries(jerrycan PRIVATE
        spdlog::spdlog
)

install(TARGETS jerrycan FILE_SET public_headers)
