# Host Software Tools
### WhiskerWire
A diagnostic tool for observing and manipulating the state of the MouseGYM modules.

### libjerrycan / pyjerrycan
A C++ Library for interacting with the hardware modules over CANbus, as well as python bindings for the library.

## Installing Dependencies

- Create a virtual environment by running `python3 -m venv .venv` from with the `software/` directory
- Run `source .venv/bin/activate` from within the `software/` directory to activate the virtual environment
- Run `pip install ./libjerrycan ./whiskerwire` from within the `software/` directory to install the `pyjerrycan`
  and `whiskerwire` python packages

## Bringing up the CAN interface

- The CAN interface `can0` must be brought up with the same configuration as JerryCAN in firmware to allow proper
  communication via the CAN protocol
- To do so, run `sudo ./can_setup.sh` from within the `software/scripts/utilities` directory

## How to use WhiskerWire (Standard)

- Bring up the CAN interface (see above section)
- Run `whiskerwire` with the virtualenv activated

## How to use (Development)

- Bring up the CAN interface (see above section)
- Run `ptw --runner "textual run --dev app.py"` from within the `/whiskerwire/whiskerwire/` directory. This will
  automatically reload the application whenever changes to a source file are saved.

## Running over SSH

- To minimize latency when running WhiskerWire over SSH, use the compression and multiplexing options, and a fast
  encryption cipher
- This is accomplished by running `ssh -C -M -c aes128-cbc leaflabs@cuanschutz-jetson`