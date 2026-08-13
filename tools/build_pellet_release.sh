#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENV_DIR="${REPO_ROOT}/firmware/.venv"
BUILD_ONLY=0
PACKAGE_ONLY=0
VERSION=""

usage() {
    cat <<'EOF'
Usage: tools/build_pellet_release.sh [--build-only | --package-only] vX.Y.Z

Build and package a tagged pellet-firmware release. With no mode option, the
script builds firmware and updater, verifies the embedded version, and creates
a flash-only release bundle under dist/.

Options:
  --build-only    Build and verify, but do not create a bundle.
  --package-only  Package already-built, version-verified outputs.

Normal release builds require HEAD to be exactly at the requested Git tag and
the tracked worktree to be clean. --package-only is intended for packaging an
already verified image after a build has completed.
EOF
}

while (($#)); do
    case "$1" in
        --build-only)
            BUILD_ONLY=1
            ;;
        --package-only)
            PACKAGE_ONLY=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        v[0-9]*.[0-9]*.[0-9]*)
            if [[ -n "${VERSION}" ]]; then
                echo "Only one version may be supplied." >&2
                exit 2
            fi
            VERSION="$1"
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if ((BUILD_ONLY && PACKAGE_ONLY)); then
    echo "--build-only and --package-only are mutually exclusive." >&2
    exit 2
fi

if [[ -z "${VERSION}" ]]; then
    echo "A release version such as v2.0.1 is required." >&2
    usage >&2
    exit 2
fi

SEMVER="${VERSION#v}"
FIRMWARE_BUILD_DIR="${REPO_ROOT}/firmware/pellet_module/build"
FIRMWARE_IMAGE="${FIRMWARE_BUILD_DIR}/pellet_module/zephyr/zephyr.signed.bin"
APP_VERSION_HEADER="${FIRMWARE_BUILD_DIR}/pellet_module/zephyr/include/generated/zephyr/app_version.h"
UPDATER_BUILD_DIR="${REPO_ROOT}/build"
UPDATER="${UPDATER_BUILD_DIR}/software/jerrycan_updater/jerrycan_updater"

if ((PACKAGE_ONLY == 0)); then
    if [[ ! -x "${VENV_DIR}/bin/west" || ! -x "${VENV_DIR}/bin/cmake" ]]; then
        echo "Build tools are missing. Run tools/setup_firmware_build_host.sh first." >&2
        exit 1
    fi

    EXACT_TAG="$(git -C "${REPO_ROOT}" describe --tags --exact-match 2>/dev/null || true)"
    if [[ "${EXACT_TAG}" != "${VERSION}" ]]; then
        echo "HEAD must be exactly tagged ${VERSION}; current exact tag is '${EXACT_TAG:-none}'." >&2
        echo "Commit the release changes, create/push ${VERSION}, then rerun this command." >&2
        exit 1
    fi

    if [[ -n "$(git -C "${REPO_ROOT}" status --porcelain --untracked-files=no)" ]]; then
        echo "Tracked files are modified. Release builds require a clean worktree." >&2
        exit 1
    fi

    if [[ -f "${REPO_ROOT}/.build-host.env" ]]; then
        # shellcheck disable=SC1091
        source "${REPO_ROOT}/.build-host.env"
        export ZEPHYR_SDK_INSTALL_DIR
    fi

    export PATH="${VENV_DIR}/bin:${PATH}"

    "${VENV_DIR}/bin/west" build \
        --build-dir "${FIRMWARE_BUILD_DIR}" \
        --sysbuild \
        --board cerebellumlab_pellet_module \
        --pristine always \
        "${REPO_ROOT}/firmware/pellet_module"

    "${VENV_DIR}/bin/cmake" \
        -S "${REPO_ROOT}" \
        -B "${UPDATER_BUILD_DIR}" \
        -GNinja \
        -DCMAKE_INSTALL_PREFIX="${REPO_ROOT}/tmp"
    "${VENV_DIR}/bin/cmake" --build "${UPDATER_BUILD_DIR}" --target jerrycan_updater
fi

if [[ ! -f "${FIRMWARE_IMAGE}" || ! -f "${APP_VERSION_HEADER}" ]]; then
    echo "The signed pellet image or generated version header is missing." >&2
    echo "Run a normal build for ${VERSION} before packaging." >&2
    exit 1
fi

if [[ ! -x "${UPDATER}" ]]; then
    echo "The JerryCAN updater is missing: ${UPDATER}" >&2
    exit 1
fi

EMBEDDED_VERSION="$(sed -n 's/^#define APP_VERSION_STRING[[:space:]]*"\([^"]*\)"/\1/p' "${APP_VERSION_HEADER}")"
if [[ "${EMBEDDED_VERSION}" != "${SEMVER}" ]]; then
    echo "Embedded firmware version '${EMBEDDED_VERSION}' does not match requested release '${SEMVER}'." >&2
    exit 1
fi

echo "Verified pellet firmware ${EMBEDDED_VERSION}: ${FIRMWARE_IMAGE}"

if ((BUILD_ONLY)); then
    exit 0
fi

case "$(uname -m)" in
    x86_64)
        PLATFORM="x86_64"
        ;;
    aarch64|arm64)
        PLATFORM="arm64"
        ;;
    *)
        echo "Unsupported updater platform: $(uname -m)" >&2
        exit 1
        ;;
esac

DIST_DIR="${REPO_ROOT}/dist"
BUNDLE_NAME="reachaq-pellet-${VERSION}-linux-${PLATFORM}"
BUNDLE_DIR="${DIST_DIR}/${BUNDLE_NAME}"
ARCHIVE="${DIST_DIR}/${BUNDLE_NAME}.tar.gz"

if [[ -e "${BUNDLE_DIR}" || -e "${ARCHIVE}" ]]; then
    echo "Release output already exists for ${BUNDLE_NAME}; refusing to overwrite it." >&2
    exit 1
fi

mkdir -p "${DIST_DIR}"
STAGING_DIR="$(mktemp -d "${DIST_DIR}/.${BUNDLE_NAME}.XXXXXX")"
cleanup() {
    if [[ -d "${STAGING_DIR}" ]]; then
        rm -rf -- "${STAGING_DIR}"
    fi
}
trap cleanup EXIT

FIRMWARE_NAME="pellet_module_fw_${VERSION}.bin"
UPDATER_NAME="jerrycan_updater_${VERSION}_linux_${PLATFORM}"
install -m 0644 "${FIRMWARE_IMAGE}" "${STAGING_DIR}/${FIRMWARE_NAME}"
install -m 0755 "${UPDATER}" "${STAGING_DIR}/${UPDATER_NAME}"
install -m 0755 "${SCRIPT_DIR}/flash_pellet_module.sh" "${STAGING_DIR}/flash_pellet_module.sh"
install -m 0755 "${SCRIPT_DIR}/reachaq-update" "${STAGING_DIR}/reachaq-update"

SOURCE_COMMIT="$(git -C "${REPO_ROOT}" rev-list -n 1 "${VERSION}")"
cat > "${STAGING_DIR}/RELEASE-MANIFEST.txt" <<EOF
reachAQ pellet firmware release: ${VERSION}
Source repository: https://github.com/Cerebellum-Lab/reachAQ-hardware
Source commit: ${SOURCE_COMMIT}
Firmware: ${FIRMWARE_NAME}
Updater: ${UPDATER_NAME}
CAN interface: can0
Nominal/data bitrates: 1000000/5000000 bit/s (CAN FD)
Default pellet-board address: 0
EOF

(
    cd "${STAGING_DIR}"
    sha256sum \
        "${FIRMWARE_NAME}" \
        "${UPDATER_NAME}" \
        flash_pellet_module.sh \
        reachaq-update \
        RELEASE-MANIFEST.txt > SHA256SUMS
)

mv "${STAGING_DIR}" "${BUNDLE_DIR}"
trap - EXIT
tar -C "${DIST_DIR}" -czf "${ARCHIVE}" "${BUNDLE_NAME}"
(
    cd "${DIST_DIR}"
    sha256sum "$(basename "${ARCHIVE}")" > "$(basename "${ARCHIVE}").sha256"
)

cat <<EOF

Release bundle created:
  ${ARCHIVE}
  ${ARCHIVE}.sha256

Flash-only rigs extract the archive and run:
  ./${BUNDLE_NAME}/reachaq-update --${SEMVER}
EOF
