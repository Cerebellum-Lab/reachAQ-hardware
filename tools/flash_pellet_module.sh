#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDRESS=0
ASSUME_YES=0
FIRMWARE=""
UPDATER=""

usage() {
    cat <<'EOF'
Usage: flash_pellet_module.sh [options]

Flash a signed pellet-module application over can0. In a release bundle, the
firmware and updater are detected automatically, so no options are normally
needed.

Options:
  -m, --address N       Pellet-board JerryCAN address (default: 0)
  -f, --firmware PATH   Signed pellet firmware image
  -u, --updater PATH    JerryCAN updater executable
  -y, --yes             Skip the interactive FLASH confirmation
  -h, --help            Show this help
EOF
}

while (($#)); do
    case "$1" in
        -m|--address)
            ADDRESS="${2:?Missing address}"
            shift
            ;;
        -f|--firmware)
            FIRMWARE="${2:?Missing firmware path}"
            shift
            ;;
        -u|--updater)
            UPDATER="${2:?Missing updater path}"
            shift
            ;;
        -y|--yes)
            ASSUME_YES=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ ! "${ADDRESS}" =~ ^[0-9]+$ ]] || ((ADDRESS < 0 || ADDRESS > 31)); then
    echo "JerryCAN address must be a decimal integer from 0 through 31." >&2
    exit 2
fi

if [[ -z "${FIRMWARE}" ]]; then
    mapfile -t FIRMWARE_CANDIDATES < <(find "${SCRIPT_DIR}" -maxdepth 1 -type f -name 'pellet_module_fw_v*.bin' -print | sort)
    if ((${#FIRMWARE_CANDIDATES[@]} != 1)); then
        echo "Expected exactly one pellet_module_fw_v*.bin beside this script; found ${#FIRMWARE_CANDIDATES[@]}." >&2
        echo "Specify one explicitly with --firmware." >&2
        exit 1
    fi
    FIRMWARE="${FIRMWARE_CANDIDATES[0]}"
fi

if [[ -z "${UPDATER}" ]]; then
    mapfile -t UPDATER_CANDIDATES < <(find "${SCRIPT_DIR}" -maxdepth 1 -type f -name 'jerrycan_updater_v*_linux_*' -perm -u+x -print | sort)
    if ((${#UPDATER_CANDIDATES[@]} != 1)); then
        echo "Expected exactly one executable jerrycan_updater_v*_linux_* beside this script; found ${#UPDATER_CANDIDATES[@]}." >&2
        echo "Specify one explicitly with --updater." >&2
        exit 1
    fi
    UPDATER="${UPDATER_CANDIDATES[0]}"
fi

if [[ ! -f "${FIRMWARE}" ]]; then
    echo "Firmware image does not exist: ${FIRMWARE}" >&2
    exit 1
fi

if [[ ! -x "${UPDATER}" ]]; then
    echo "Updater is not executable: ${UPDATER}" >&2
    exit 1
fi

if [[ -f "${SCRIPT_DIR}/SHA256SUMS" ]]; then
    echo "Verifying release bundle checksums..."
    (
        cd "${SCRIPT_DIR}"
        sha256sum --check SHA256SUMS
    )
fi

if ! command -v ip >/dev/null 2>&1; then
    echo "The 'ip' command is required to inspect can0." >&2
    exit 1
fi

CAN_DETAILS="$(ip -details link show can0 2>/dev/null || true)"
if [[ -z "${CAN_DETAILS}" ]]; then
    echo "can0 does not exist. Configure the rig's CAN adapter before flashing." >&2
    exit 1
fi

if [[ "${CAN_DETAILS}" != *"UP"* || "${CAN_DETAILS}" != *"can <FD>"* ||
      "${CAN_DETAILS}" != *"bitrate 1000000"* || "${CAN_DETAILS}" != *"dbitrate 5000000"* ]]; then
    echo "can0 is not UP as CAN FD at 1 Mbit/s nominal and 5 Mbit/s data." >&2
    echo "Do not flash until the interface configuration is corrected." >&2
    exit 1
fi

"${UPDATER}" --version

cat <<EOF

Ready to update the pellet board.
  Address:  ${ADDRESS}
  Firmware: ${FIRMWARE}
  Updater:  ${UPDATER}

Close reachAQ before continuing, but leave the service that configures can0
running. Keep board power and CAN connected through transfer, reboot, and
finalization. A 40-60 second quiet transfer is normal.
EOF

if ((ASSUME_YES == 0)); then
    read -r -p "Type FLASH to continue: " CONFIRMATION
    if [[ "${CONFIRMATION}" != "FLASH" ]]; then
        echo "Update cancelled."
        exit 1
    fi
fi

"${UPDATER}" -m "${ADDRESS}" -f "${FIRMWARE}"

cat <<'EOF'

Firmware update completed. Reopen reachAQ and verify board discovery/version,
then test Tone 1 -> STIM0 and Tone 2 -> STIM1 on the acquisition inputs.
EOF
