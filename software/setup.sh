#!/bin/bash

RED="\e[31;1m"
GREEN="\e[92;1m"
YELLOW="\e[33;1m"
CYAN="\e[96;1m"
ENDCOLOR="\e[0m"

echo -e "${CYAN}Setting up MouseGYM...${ENDCOLOR}"

# Run this script as sudo
if [ "$EUID" -ne 0 ]
  then echo -e "${RED}Please run as root${ENDCOLOR}"
  exit
fi

echo -e "${CYAN}Bringing up CAN interface...${ENDCOLOR}"

./scripts/utilities/can_setup.sh

if [ $? -eq 0 ]; then
    echo -e "${GREEN}CAN interface brought up successfully!${ENDCOLOR}"
else
    echo -e "${RED}Failed to bring up CAN interface - Script failed with exit code $?${ENDCOLOR}"
    exit    
fi

echo -e "${CYAN}Activating WhiskerWire Virtual Environment...${ENDCOLOR}"

if [ -n "$VIRTUAL_ENV" ]; then
    echo -e "${YELLOW}Detected active Virtual Environment - Deactivating before continuing...${ENDCOLOR}"
    deactivate
fi

source ./whiskerwire/.venv/bin/activate

echo -e "${GREEN}Virtual Environment activated successfully!${ENDCOLOR}"

echo -e "${CYAN}Installing Python requirements...${ENDCOLOR}"

pip install -r ./whiskerwire/requirements.txt

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Requirements installed successfully!${ENDCOLOR}"
else
    echo -e "${RED}Failed to install requirements - pip failed with exit code $?${ENDCOLOR}"
    exit
fi

echo -e "${CYAN}Installing PyJerryCAN...${ENDCOLOR}"

PYJERRYCAN_VERSION=$(pip show "pyjerrycan" 2>/dev/null | grep "^Version:" | awk '{print $2}')

# Check if the requirement is installed
if [ -n "$PYJERRYCAN_VERSION" ]; then
    echo -e "${GREEN}Existing installation of PyJerryCAN found in Virtual Environment: pyjerrycan==$PYJERRYCAN_VERSION${ENDCOLOR}"
    echo -e "${YELLOW}If you are unsure if this the latest version, run 'pip install -e ./libjerrycan' - or run 'pip uninstall pyjerrycan' and then run this script again${ENDCOLOR}"
else
    pip install -e ./libjerrycan
    if [ $? -eq 0 ]; then
	    echo -e "${GREEN}PyJerryCAN installed successfully!${ENDCOLOR}"
    else
	    echo -e "${RED}PyJerryCAN installation failed with exit code $?${ENDCOLOR}"
    	    exit
    fi
fi

echo -e "${GREEN}MouseGYM setup completed successfully!${ENDCOLOR}"

