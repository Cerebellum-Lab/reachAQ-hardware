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

if [ -d "whiskerwire/.venv" ]; then
  echo -e "${GREEN}Found existing Virtual Environment!${ENDCOLOR}"
else
    echo -e "${CYAN}Could not find Virtual Environment, creating new one...${ENDCOLOR}"
    echo -e "${YELLOW}Ignore the above message if this is your first time running this script${ENDCOLOR}"
	
    # Create Virtual Environment if it does not exist
    python3 -m venv ./whiskerwire/.venv

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Virtual Environment created successfully!${ENDCOLOR}"
    else
        echo -e "${RED}Creation of Virtual Environment failed with exit code $?${ENDCOLOR}"
        exit
    fi
fi

# Activate Virtual Environment
source ./whiskerwire/.venv/bin/activate

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Virtual Environment activated successfully!${ENDCOLOR}"
else
    echo -e "${RED}Virtual Environment activation failed with exit code $?${ENDCOLOR}"
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

echo -e "${CYAN}Installing WhiskerWire...${ENDCOLOR}"

WHISKERWIRE_VERSION=$(pip show "whiskerwire" 2>/dev/null | grep "^Version:" | awk '{print $2}')

if [ -n "$WHISKERWIRE_VERSION" ]; then
    echo -e "${GREEN}Existing installation of WhiskerWire found in Virtual Environment: whiskerwire==$WHISKERWIRE_VERSION${ENDCOLOR}"
    echo -e "${YELLOW}If you are unsure if this the latest version, run 'pip install -e ./whiskerwire' - or run 'pip uninstall whiskerwire' and then run this script again${ENDCOLOR}"
else
    pip install -e ./whiskerwire
    if [ $? -eq 0 ]; then
	echo -e "${GREEN}WhiskerWire installed successfully!${ENDCOLOR}"
    else
	echo -e "${RED}WhiskerWire installation failed with exit code $?${ENDCOLOR}"
	exit
    fi
fi

echo -e "${GREEN}MouseGYM setup completed successfully!${ENDCOLOR}"

