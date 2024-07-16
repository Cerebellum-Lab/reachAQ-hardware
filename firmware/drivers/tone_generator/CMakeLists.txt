zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LL_TONE_GENERATOR tone_generator.c)
