cmake_minimum_required(VERSION 3.20)

project(jerrycan_updater)

# Use Boost for program options
set(BOOST_INCLUDE_LIBRARIES program_options)
set(BOOST_ENABLE_CMAKE on)
include(FetchContent)
FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG boost-1.86.0
        GIT_SHALLOW 1
)
FetchContent_MakeAvailable(Boost)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.x
)
FetchContent_MakeAvailable(spdlog)

add_executable(jerrycan_updater main.cpp)
target_link_libraries(jerrycan_updater PRIVATE jerrycan Boost::program_options spdlog::spdlog)

