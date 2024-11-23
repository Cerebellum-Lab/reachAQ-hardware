# WhiskerWire
A diagnostic tool for observing and manipulating the state of the MouseGYM modules.

## Installing Dependencies
- Create a virtual environment by running `python3 -m venv .venv` from with the `software/` directory
- Run `source .venv/bin/activate` from within the `software/` directory to activate the virtual environment
- Run `pip install -r requirements.txt` from within `software/whiskerwire/` to install WhiskerWire Python dependencies
- Run `pip install -e .` from within the `software/libjerrycan/` directory to install the `pyjerrycan` library

## Bringing up the CAN interface
- The CAN interface `can0` must be brought up with the same configuration as JerryCAN in firmware to allow proper communication via the CAN protocol
- To do so, run `sudo ./can_setup.sh` from within the `software/scripts/utilities` directory

## How to use (Standard)
- Bring up the CAN interface (see above section)
- Run `python3 app.py` from within the `software/whiskerwire/whiskerwire/` directory

## How to use (Development)
- Bring up the CAN interface (see above section)
- Run `ptw --runner "textual run --dev app.py"` from within the `/whiskerwire/whiskerwire/` directory. This will automatically reload the application whenever changes to a source file are saved.

## Running over SSH
- To minimize latency when running WhiskerWire over SSH, use the compression and multiplexing options, and a fast encryption cipher
- This is accomplished by running `ssh -C -M -c aes128-cbc leaflabs@cuanschutz-jetson`