# Host Software Tools
### WhiskerWire
A diagnostic tool for observing and manipulating the state of the MouseGYM modules.

### libjerrycan / pyjerrycan
A C++ Library for interacting with the hardware modules over CANbus, as well as python bindings for the library.

## How to use WhiskerWire (Standard)

- Run `sudo ./setup.sh` to bring up the CAN interface and install the `pyjerrycan` and `whiskerwire` packages
- Run `source ./whiskerwire/.venv/bin/activate` to activate the virtual environment
- Run `whiskerwire` with the virtual environment activated

## Running over SSH

- To minimize latency when running WhiskerWire over SSH, use the compression and multiplexing options, and a fast
  encryption cipher
- This is accomplished by running `ssh -C -M -c aes128-cbc leaflabs@cuanschutz-jetson`