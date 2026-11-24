#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENV_DIR="${REPO_ROOT}/firmware/.venv"
BUILD_ONLY=0
PACKAGE_ONLY=0
VERSION=""
PUBLIC_KEY="${REACHAQ_PELLET_SIGNING_PUBKEY:-}"
SIGNING_KEY="${REACHAQ_PELLET_SIGNING_KEY:-}"

usage() {
    cat <<'EOF'
Usage: tools/build_pellet_release.sh [--build-only | --package-only]
           [--public-key PATH] [--signing-key PATH] vX.Y.Z

Build and package a tagged pellet-firmware release. With no mode option, the
script builds firmware and updater, verifies the embedded version, and creates
a flash-only release bundle under dist/.

Options:
  --build-only    Build and verify, but do not create a bundle.
  --package-only  Package already-built, version-verified outputs.
  --public-key PATH
                  Active pellet public key embedded in MCUboot. Default:
                  $REACHAQ_PELLET_SIGNING_PUBKEY. Required to build.
  --signing-key PATH
                  Encrypted pellet private key, outside every repository
                  (the mounted signing USB). Default: $REACHAQ_PELLET_SIGNING_KEY.
                  Required for a full release; not used by --build-only.

The application is built unsigned. A full release signs it with the private
key (imgtool asks for the passphrase twice, once per image format) and every
packaged image is verified against the key MCUboot embeds.

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
        --public-key)
            PUBLIC_KEY="${2:?--public-key needs a path}"
            shift
            ;;
        --signing-key)
            SIGNING_KEY="${2:?--signing-key needs a path}"
            shift
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

SIGNING_TOOL="${SCRIPT_DIR}/firmware_signing.py"

if ((PACKAGE_ONLY == 0)); then
    if [[ ! -x "${VENV_DIR}/bin/west" || ! -x "${VENV_DIR}/bin/cmake" ]]; then
        echo "Build tools are missing. Run tools/setup_firmware_build_host.sh first." >&2
        exit 1
    fi

    if [[ -z "${PUBLIC_KEY}" ]]; then
        echo "No pellet public key. Pass --public-key or set REACHAQ_PELLET_SIGNING_PUBKEY." >&2
        exit 1
    fi
    if ((BUILD_ONLY == 0)) && [[ -z "${SIGNING_KEY}" ]]; then
        echo "A release needs the private key. Mount the signing USB and pass --signing-key" >&2
        echo "or set REACHAQ_PELLET_SIGNING_KEY. Use --build-only for an unsigned build." >&2
        exit 1
    fi
    "${VENV_DIR}/bin/python" "${SIGNING_TOOL}" check-public "${PUBLIC_KEY}"

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
        "${REPO_ROOT}/firmware/pellet_module" \
        -- "-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"${PUBLIC_KEY}\""

    if ((BUILD_ONLY == 0)); then
        "${VENV_DIR}/bin/python" "${SIGNING_TOOL}" sign \
            --build-dir "${FIRMWARE_BUILD_DIR}" --key "${SIGNING_KEY}"
    fi

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
    echo "Build-only output is unsigned and will not boot; sign it before flashing."
    exit 0
fi

if ! SIGNING_KEY_RECORD="$(PATH="${VENV_DIR}/bin:${PATH}" "${VENV_DIR}/bin/python" "${SIGNING_TOOL}" verify --build-dir "${FIRMWARE_BUILD_DIR}")"; then
    echo "The pellet image is not signed by an active key; refusing to package it." >&2
    exit 1
fi
echo "${SIGNING_KEY_RECORD}"
SIGNING_KEY_ID="${SIGNING_KEY_RECORD#Verified against }"

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
Signing key: ${SIGNING_KEY_ID}
CAN interface: can0
Nominal/data bitrates: 1000000/5000000 bit/s (CAN FD)
Default pellet-board address: 0
EOF

cat > "${STAGING_DIR}/README-FIRST.txt" <<EOF
reachAQ pellet firmware ${VERSION}

For rig operators:

1. Update reachAQ on this rig first (reachaq-sync). reachAQ refuses any
   firmware version it does not list, so the updater checks and will not
   flash ${SEMVER} until this rig's reachAQ lists it.
2. Close the reachAQ application. Leave reachaq-can.service running.
3. Keep the pellet board powered and connected to CAN.
4. From this directory run:

       ./reachaq-update --${SEMVER}

5. Type FLASH when prompted and wait for "JerryCAN update complete".
   A quiet 40-60 second transfer is normal. Do not disconnect power or CAN.
6. Start reachAQ (reachaq, or the desktop icon), confirm the pellet controller
   connects, and test Tone 1 -> STIM0 and Tone 2 -> STIM1 in the acquisition
   system.

If an error occurs, save the complete terminal output and stop. Do not try a
different firmware version during troubleshooting.
EOF

(
    cd "${STAGING_DIR}"
    sha256sum \
        "${FIRMWARE_NAME}" \
        "${UPDATER_NAME}" \
        flash_pellet_module.sh \
        reachaq-update \
        README-FIRST.txt \
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
