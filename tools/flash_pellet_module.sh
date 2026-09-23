#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDRESS=0
ASSUME_YES=0
ALLOW_UNQUALIFIED=0
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
      --allow-unqualified
                        Flash even though this rig's reachAQ does not list the
                        firmware version. reachAQ refuses such a board until
                        its compatibility list names the version, so use this
                        only to bring up a release that is being qualified.
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
        --allow-unqualified)
            ALLOW_UNQUALIFIED=1
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

# reachAQ refuses any pellet firmware version its compatibility list does not
# name, so a board flashed ahead of that list is refused at the next startup.
# That happened on christielab10 on 2026-09-23. Ask reachAQ itself, through
# the module the application uses, from every checkout it can start from: the
# Conda environment's installed package, which the environment's own `reachaq`
# command imports, and the repository in launcher.conf, which the desktop icon
# and ~/.local/bin/reachaq run. They are normally one checkout; that day they
# were two, and only checking one would have missed it.
HOST_PROBE='
import sys
import tools.acquisition.model.firmware_compatibility as policy
result = policy.FirmwareCompatibilityPolicy.load().evaluate(sys.argv[1])
print("REACHAQ_POLICY", "listed" if result.supported else "unlisted", policy.__file__)
'
POLICY_MODULE_SUFFIX="/tools/acquisition/model/firmware_compatibility.py"

refuse_unqualified() {
    if ((ALLOW_UNQUALIFIED)); then
        echo "Continuing anyway because --allow-unqualified was given." >&2
        echo "reachAQ will refuse this board until its list names the version." >&2
        HOST_STATUS="NOT accepted by this rig's reachAQ (--allow-unqualified)"
        return
    fi
    echo "Update reachAQ first (reachaq-sync), then flash. To flash a release" >&2
    echo "that is still being qualified, pass --allow-unqualified." >&2
    exit 1
}

check_host_qualification() {
    local version="$1"
    local launcher_config="${REACHAQ_LAUNCHER_CONFIG:-${HOME}/.config/reachaq/launcher.conf}"
    local conda_bin="" conda_env="" repository="" key value candidate
    if [[ -r "${launcher_config}" ]]; then
        while IFS='=' read -r key value || [[ -n "${key:-}" ]]; do
            value="${value%$'\r'}"
            case "${key}" in
                CONDA_BIN) conda_bin="${value}" ;;
                CONDA_ENV) conda_env="${value}" ;;
                REPOSITORY) repository="${value}" ;;
            esac
        done < "${launcher_config}"
    fi
    if [[ -z "${conda_bin}" || ! -x "${conda_bin}" ]]; then
        conda_bin="$(command -v conda || true)"
    fi
    if [[ -z "${conda_bin}" ]]; then
        for candidate in "${HOME}/anaconda3/bin/conda" "${HOME}/miniconda3/bin/conda" \
                "${HOME}/mambaforge/bin/conda"; do
            if [[ -x "${candidate}" ]]; then
                conda_bin="${candidate}"
                break
            fi
        done
    fi
    conda_env="${conda_env:-reachaq}"

    # From / the import resolves through the installed package; from the
    # launcher's repository it resolves there first, exactly as the launcher's
    # own `python -m reachAQ.app` does.
    local origins=(/)
    if [[ -n "${repository}" && -d "${repository}" ]]; then
        origins+=("${repository}")
    fi

    local -A seen=()
    local listed=() unlisted=() origin output marker status path
    if [[ -n "${conda_bin}" ]]; then
        for origin in "${origins[@]}"; do
            output="$(cd "${origin}" && "${conda_bin}" run -n "${conda_env}" \
                python -c "${HOST_PROBE}" "${version}" 2>/dev/null)" || true
            read -r marker status path < <(grep '^REACHAQ_POLICY ' <<<"${output}" | tail -n 1) || true
            if [[ "${marker:-}" != "REACHAQ_POLICY" || -n "${seen[${path}]:-}" ]]; then
                marker=""
                continue
            fi
            seen["${path}"]=1
            if [[ "${status}" == "listed" ]]; then
                listed+=("${path%"${POLICY_MODULE_SUFFIX}"}")
            else
                unlisted+=("${path%"${POLICY_MODULE_SUFFIX}"}")
            fi
            marker=""
        done
    fi

    if ((${#listed[@]} == 0 && ${#unlisted[@]} == 0)); then
        echo "Could not ask reachAQ whether it accepts pellet firmware v${version}." >&2
        echo "Looked for Conda environment '${conda_env}' using '${conda_bin:-no conda found}'." >&2
        refuse_unqualified
        return
    fi
    if ((${#unlisted[@]})); then
        echo "reachAQ on this rig does not accept pellet firmware v${version}:" >&2
        for path in "${unlisted[@]}"; do
            echo "  not listed by the checkout at ${path}" >&2
        done
        for path in "${listed[@]}"; do
            echo "  listed by the checkout at ${path}" >&2
        done
        echo "A board flashed now is refused when reachAQ starts from an unlisted checkout." >&2
        refuse_unqualified
        return
    fi
    HOST_STATUS="accepts v${version} (${listed[*]})"
}

HOST_STATUS=""
FIRMWARE_BASENAME="$(basename -- "${FIRMWARE}")"
if [[ "${FIRMWARE_BASENAME}" =~ ^pellet_module_fw_v([0-9]+\.[0-9]+\.[0-9]+)\.bin$ ]]; then
    echo "Asking reachAQ whether it accepts pellet firmware v${BASH_REMATCH[1]}..."
    check_host_qualification "${BASH_REMATCH[1]}"
else
    # Release images are named by the version the build verified is embedded,
    # so the name is trustworthy; any other name says nothing about it.
    echo "Cannot tell the firmware version from '${FIRMWARE_BASENAME}', so cannot" >&2
    echo "check that reachAQ accepts it. Release images are named" >&2
    echo "pellet_module_fw_vX.Y.Z.bin." >&2
    refuse_unqualified
fi

"${UPDATER}" --version

cat <<EOF

Ready to update the pellet board.
  Address:  ${ADDRESS}
  Firmware: ${FIRMWARE}
  Updater:  ${UPDATER}
  reachAQ:  ${HOST_STATUS}

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

Firmware update completed. Start reachAQ with the operator's own command
(reachaq, or the desktop icon) and confirm the pellet controller connects,
then test Tone 1 -> STIM0 and Tone 2 -> STIM1 on the acquisition inputs.
EOF
