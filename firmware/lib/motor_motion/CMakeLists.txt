zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources(
        motor_motion.c
)

zephyr_library_sources_ifdef(CONFIG_LIB_MOTOR_MOTION_SHELL
        motor_motion_shell.c
)