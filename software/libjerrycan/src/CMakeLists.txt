include(FetchContent)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.x
)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

FetchContent_MakeAvailable(spdlog)

add_library(jerrycan STATIC
        libjerrycan.cpp
)

target_include_directories(jerrycan PUBLIC
        ../../../firmware/lib/jerrycan
)

target_link_libraries(jerrycan PRIVATE
        spdlog::spdlog
)
