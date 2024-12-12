# Host Software Tools
### WhiskerWire
A diagnostic tool for observing and manipulating the state of the MouseGYM modules.

### libjerrycan / pyjerrycan
A C++ Library for interacting with the hardware modules over CANbus, as well as python bindings for the library.

## Bringing up the CAN Interface
- Run `sudo scripts/utilities/can_setup.sh` to bring up `can0`

## Bringing up the Virtual Environment
- If a virtual environment does not yet exist, create one by running `python3 -m venv .venv`
- To activate the virtual environment, run `source .venv/bin/activate`

## Installing Dependencies
- With the virtual environment activated, run `pip install -r requirements.txt`

## How to use WhiskerWire
- With the CAN interface brought up and the virtual environment activated, run `whiskerwire`

## Running over SSH
- To minimize latency when running WhiskerWire over SSH, use the compression and multiplexing options, and a fast
  encryption cipher
- This is accomplished by running `ssh -C -M -c aes128-gcm@openssh.com leaflabs@cuanschutz-jetson`