zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources(
	fft.c
)

zephyr_library_sources_ifdef(CONFIG_LIB_FFT_SHELL
	fft_shell.c
)