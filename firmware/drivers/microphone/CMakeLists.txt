if(CONFIG_DT_HAS_LL_MICROPHONE_ENABLED)
	zephyr_library()

	zephyr_include_directories(.)

	zephyr_library_sources_ifdef(CONFIG_LIB_MIC microphone.c)

	zephyr_library_sources_ifdef(CONFIG_LIB_MIC_SHELL microphone_shell.c)
endif()