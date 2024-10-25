zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LL_ANALOG_OUT analog_out.c)

zephyr_library_sources_ifdef(CONFIG_LL_ANALOG_OUT_SHELL analog_out_shell.c)
