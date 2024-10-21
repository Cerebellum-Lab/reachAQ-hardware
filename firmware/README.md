# Autotrainer Firmware

## Setup

This project is built on the Zephyr RTOS. It uses the West tool in order to handle various project tasks,
such as building and flashing.

See the Zephyr Getting Started Guide for how to install the various tools needed for developers
https://docs.zephyrproject.org/latest/develop/getting_started/index.html

It is recommended to do `west config build.sysbuild True` to have West default to using sysbuild at all times.

Once West is installed, you must initialize the project workspace from the `firmware` directory by doing a checkout of zephyr and all dependent modules:

```bash
west init -l
west update
```

And you should install the python requirements:
```bash
pip install -r requirements.txt
```

## Building

For each board, there are two firmware images that need to be built. MCUBoot and our application code.
West implements sysbuild which can help coordinate building both of these projects for us.

The first time building the project (or if you ever delete the `build` directory), run this command from
the `<board>_module` directory:

```bash
west build --sysbuild --board cerebellumlab_<board>_module -p
```

Note that this may not work inside a venv.

Subsequent builds can just be run using `west build`

## Flashing

You have to download the pack for pyocd for our particular board:
```bash
pyocd pack install stm32g4
```

Use the `west flash` command to flash MCUBoot (the bootloader) and the application image.

