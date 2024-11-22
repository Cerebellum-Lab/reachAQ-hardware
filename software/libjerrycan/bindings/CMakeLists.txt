include(FetchContent)
FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11
        GIT_TAG v2.13.5
)

FetchContent_MakeAvailable(pybind11)

pybind11_add_module(pyjerrycan
        pyjerrycan.cpp
)

target_link_libraries(pyjerrycan PRIVATE jerrycan)

add_custom_command(TARGET pyjerrycan POST_BUILD
        COMMAND stubgen
        ARGS -m pyjerrycan -o .
        BYPRODUCTS pyjerrycan.pyi
)

include(GNUInstallDirs)
install(TARGETS pyjerrycan
        COMPONENT python
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
