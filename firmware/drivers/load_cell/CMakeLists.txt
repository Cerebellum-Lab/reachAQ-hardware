zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_LL_LOAD_CELL load_cell.c nau7802.c)

zephyr_library_sources_ifdef(CONFIG_LL_LOAD_CELL_SHELL load_cell_shell.c)
