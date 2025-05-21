if(CONFIG_DT_HAS_LL_PRESSURE_SENSOR_ENABLED)
	zephyr_library()

	zephyr_include_directories(.)

	zephyr_library_sources_ifdef(CONFIG_LL_PRESSURE_SENSOR pressure_sensor.c)

	zephyr_library_sources_ifdef(CONFIG_LL_PRESSURE_SENSOR_SHELL pressure_sensor_shell.c)
endif()