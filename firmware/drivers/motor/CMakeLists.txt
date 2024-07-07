zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LL_SERVO servo.c)
zephyr_library_sources_ifdef(CONFIG_LL_STEPPER stepper.c)
