zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources(
        jerrycan.c
)

zephyr_library_sources_ifdef(CONFIG_LIB_JERRYCAN_SHELL
        jerrycan_shell.c
)