#!/bin/bash

# Define colors for formatted output messages
RED="\e[31;1m"
GREEN="\e[92;1m"
YELLOW="\e[33;1m"
CYAN="\e[96;1m"
ENDCOLOR="\e[0m"

# Start of the setup script
echo -e "${CYAN}Setting up MouseGYM...${ENDCOLOR}"

# Check if the script is run with sudo or as root
if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}Please run as root${ENDCOLOR}"
  exit
fi

# Bring up the CAN interface
echo -e "${CYAN}Bringing up CAN interface...${ENDCOLOR}"

./scripts/utilities/can_setup.sh &>/dev/null

# Check if the CAN setup script ran successfully
if [ $? -eq 0 ]; then
    echo -e "${GREEN}\tCAN interface brought up successfully!${ENDCOLOR}"
else
    echo -e "${RED}\tFailed to bring up CAN interface - Script failed with exit code $?${ENDCOLOR}"
    exit    
fi

# Activate the virtual environment for WhiskerWire
echo -e "${CYAN}Activating WhiskerWire Virtual Environment...${ENDCOLOR}"

# Deactivate any existing virtual environment
if [ -n "$VIRTUAL_ENV" ]; then
    echo -e "${YELLOW}\tDetected active Virtual Environment - Deactivating before continuing...${ENDCOLOR}"
    deactivate
fi

# Check if the virtual environment exists
if [ -d "whiskerwire/.venv" ]; then
    echo -e "${GREEN}\tFound existing Virtual Environment!${ENDCOLOR}"
else
    # Create a new virtual environment if one does not exist
    echo -e "${CYAN}\tCould not find Virtual Environment, creating new one...${ENDCOLOR}"
    echo -e "${YELLOW}\tIgnore the above message if this is your first time running this script${ENDCOLOR}"

    python3 -m venv ./whiskerwire/.venv

    # Check if the virtual environment was created successfully
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}\tVirtual Environment created successfully!${ENDCOLOR}"
    else
        echo -e "${RED}\tCreation of Virtual Environment failed with exit code $?${ENDCOLOR}"
        exit
    fi
fi

# Activate the virtual environment
source ./whiskerwire/.venv/bin/activate

# Check if the activation was successful
if [ $? -eq 0 ]; then
    echo -e "${GREEN}\tVirtual Environment activated successfully!${ENDCOLOR}"
else
    echo -e "${RED}\tVirtual Environment activation failed with exit code $?${ENDCOLOR}"
    exit
fi

# Install PyJerryCAN
echo -e "${CYAN}Installing PyJerryCAN...${ENDCOLOR}"

# Check if PyJerryCAN is already installed in the virtual environment
PYJERRYCAN_VERSION=$(pip show "pyjerrycan" 2>/dev/null | grep "^Version:" | awk '{print $2}')

if [ -n "$PYJERRYCAN_VERSION" ]; then
    # If PyJerryCAN is found, notify the user
    echo -e "${GREEN}\tExisting installation of PyJerryCAN found in Virtual Environment: pyjerrycan==$PYJERRYCAN_VERSION${ENDCOLOR}"
    echo -e "${YELLOW}\tIf you are unsure if this is the latest version, run 'pip install -e ./libjerrycan' - or run 'pip uninstall pyjerrycan' and then run this script again${ENDCOLOR}"
else
    # If not installed, install PyJerryCAN
    pip install -e ./libjerrycan
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}\tPyJerryCAN installed successfully!${ENDCOLOR}"
    else
        echo -e "${RED}\tPyJerryCAN installation failed with exit code $?${ENDCOLOR}"
        exit
    fi
fi

# Install WhiskerWire
echo -e "${CYAN}Installing WhiskerWire...${ENDCOLOR}"

# Check if WhiskerWire is already installed in the virtual environment
WHISKERWIRE_VERSION=$(pip show "whiskerwire" 2>/dev/null | grep "^Version:" | awk '{print $2}')

if [ -n "$WHISKERWIRE_VERSION" ]; then
    # If WhiskerWire is found, notify the user
    echo -e "${GREEN}\tExisting installation of WhiskerWire found in Virtual Environment: whiskerwire==$WHISKERWIRE_VERSION${ENDCOLOR}"
    echo -e "${YELLOW}\tIf you are unsure if this is the latest version, run 'pip install -e ./whiskerwire' - or run 'pip uninstall whiskerwire' and then run this script again${ENDCOLOR}"
else
    # If not installed, install WhiskerWire
    pip install -e ./whiskerwire
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}\tWhiskerWire installed successfully!${ENDCOLOR}"
    else
        echo -e "${RED}\tWhiskerWire installation failed with exit code $?${ENDCOLOR}"
        exit
    fi
fi

# End of the setup script
echo -e "${GREEN}MouseGYM setup completed successfully!${ENDCOLOR}"

