zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LL_SERVO servo.c)
zephyr_library_sources_ifdef(CONFIG_LL_STEPPER stepper.c)
zephyr_library_sources_ifdef(CONFIG_ADI_TMC2209 adi_tmc2209.c)
zephyr_library_sources(motor_common.c)

zephyr_library_sources_ifdef(CONFIG_LL_SERVO_SHELL servo_shell.c)
