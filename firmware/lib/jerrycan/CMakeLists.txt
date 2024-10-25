zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources(
        jerrycan.c
        modules/heartbeat.c
        modules/status.c
        modules/gpio.c
)

# If this board has the motor motion library enabled, include the stepper and servo files
# to include the appropriate CAN message handlers
if (CONFIG_LIB_MOTOR_MOTION)
    zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_SERVO_ENABLED
            modules/servo.c
    )

    zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_STEPPER_ENABLED
            modules/stepper.c
    )
endif ()

# If this board has a temperature sensor, include the temperature file
# I'm not sure there is a generic temperature CONFIG_ that is defined for this
# so we'll just be specific to the sensor in use right now
zephyr_library_sources_ifdef(CONFIG_DT_HAS_SILABS_SI7006_ENABLED
        modules/temperature.c
)

# If this board has a tone generator...
zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_TONE_GENERATOR_ENABLED
        modules/tone.c
)

# If this board has a pressure sensor...
zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_PRESSURE_SENSOR_ENABLED
        modules/pressure.c
)

# If this board has a load cell...
zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_LOAD_CELL_ENABLED
        modules/load_cell.c
)

# If this board has an analog output...
zephyr_library_sources_ifdef(CONFIG_DT_HAS_LL_ANALOG_OUT_ENABLED
        modules/analog.c
)

zephyr_library_sources_ifdef(CONFIG_LIB_JERRYCAN_SHELL
        jerrycan_shell.c
)
