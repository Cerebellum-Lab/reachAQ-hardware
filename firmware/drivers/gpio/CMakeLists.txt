zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LL_GENERIC_GPIO generic_gpios.c)
