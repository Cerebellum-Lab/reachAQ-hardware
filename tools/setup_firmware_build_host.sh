#!/usr/bin/env bash

set -euo pipefail

SDK_VERSION="0.16.8"
CMAKE_VERSION="3.31.2"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_PARENT="$(dirname "${REPO_ROOT}")"
VENV_DIR="${REPO_ROOT}/firmware/.venv"
SDK_DIR="${ZEPHYR_SDK_INSTALL_DIR:-${REPO_PARENT}/zephyr-sdk-${SDK_VERSION}}"
DEFAULT_SDK_DIR="${REPO_PARENT}/zephyr-sdk-${SDK_VERSION}"
INSTALL_SYSTEM_PACKAGES=1

usage() {
    cat <<'EOF'
Usage: tools/setup_firmware_build_host.sh [--skip-system-packages]

Prepare the one designated firmware build rig. The script is idempotent and:
  * installs the Ubuntu host packages (unless explicitly skipped),
  * creates firmware/.venv with West and CMake 3.31.2,
  * initializes and updates the Zephyr West workspace,
  * installs Zephyr SDK 0.16.8 with the ARM toolchain, and
  * records the SDK location in .build-host.env.

Set ZEPHYR_SDK_INSTALL_DIR before running to choose a non-default SDK path.
EOF
}

while (($#)); do
    case "$1" in
        --skip-system-packages)
            INSTALL_SYSTEM_PACKAGES=0
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

if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "This setup helper currently supports the x86-64 Zephyr SDK build host." >&2
    echo "Use the manual workflow for architecture: $(uname -m)" >&2
    exit 1
fi

if ((INSTALL_SYSTEM_PACKAGES)); then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "Automatic system-package setup currently supports Ubuntu/Debian only." >&2
        echo "Install the packages listed in docs/pellet-firmware-release-and-deployment.md, then rerun with --skip-system-packages." >&2
        exit 1
    fi

    sudo apt-get update
    sudo apt-get install -y --no-install-recommends \
        build-essential \
        ccache \
        cmake \
        device-tree-compiler \
        dfu-util \
        file \
        git \
        gperf \
        libmagic1 \
        libsdl2-dev \
        ninja-build \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
        wget \
        xz-utils
fi

python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/python" -m pip install --upgrade pip
"${VENV_DIR}/bin/python" -m pip install \
    "cmake==${CMAKE_VERSION}" \
    west \
    -r "${REPO_ROOT}/firmware/requirements.txt"

if [[ ! -d "${REPO_ROOT}/.west" ]]; then
    (
        cd "${REPO_ROOT}"
        "${VENV_DIR}/bin/west" init -l firmware
    )
fi

(
    cd "${REPO_ROOT}"
    "${VENV_DIR}/bin/west" update
)

SDK_ARCHIVE="${REPO_PARENT}/zephyr-sdk-${SDK_VERSION}_linux-x86_64_minimal.tar.xz"
SDK_URL="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VERSION}/$(basename "${SDK_ARCHIVE}")"

if [[ ! -x "${SDK_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ]]; then
    if [[ ! -f "${SDK_ARCHIVE}" ]]; then
        wget --continue --output-document "${SDK_ARCHIVE}.part" "${SDK_URL}"
        mv "${SDK_ARCHIVE}.part" "${SDK_ARCHIVE}"
    fi

    if [[ ! -d "${SDK_DIR}" ]]; then
        if [[ "${SDK_DIR}" == "${DEFAULT_SDK_DIR}" ]]; then
            tar -C "${REPO_PARENT}" -xf "${SDK_ARCHIVE}"
        else
            SDK_STAGING_DIR="$(mktemp -d)"
            tar -C "${SDK_STAGING_DIR}" -xf "${SDK_ARCHIVE}"
            mkdir -p "$(dirname "${SDK_DIR}")"
            mv "${SDK_STAGING_DIR}/zephyr-sdk-${SDK_VERSION}" "${SDK_DIR}"
            rmdir "${SDK_STAGING_DIR}"
        fi
    fi

    "${SDK_DIR}/setup.sh" -t arm-zephyr-eabi -c -h
fi

printf 'export ZEPHYR_SDK_INSTALL_DIR=%q\n' "${SDK_DIR}" > "${REPO_ROOT}/.build-host.env"

cat <<EOF

Build-host setup complete.

  West:       ${VENV_DIR}/bin/west
  CMake:      ${VENV_DIR}/bin/cmake
  Zephyr SDK: ${SDK_DIR}

For a tagged release, run:
  tools/build_pellet_release.sh vX.Y.Z
EOF
