zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LIB_MIC mic.c)

zephyr_library_sources_ifdef(CONFIG_LIB_MIC_SHELL mic_shell.c)
