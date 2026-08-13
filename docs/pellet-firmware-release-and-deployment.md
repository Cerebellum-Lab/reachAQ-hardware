# Pellet firmware release and deployment

This document defines the Cerebellum Lab workflow for building, releasing, and
deploying pellet-board firmware. The intended operating model is:

- one designated **build rig** has compilers, West, the Zephyr SDK, and the
  source repository;
- every other **flash-only rig** receives a versioned release bundle containing
  the signed firmware, a prebuilt JerryCAN updater, checksums, and a one-command
  flash script.

Flash-only rigs do not need Git, CMake, West, Python, or the Zephyr SDK.

## Released v2.0.0 behavior

Release `v2.0.0` is based on commit
`3c0d93e6bb53dc4d0adbf21c213ded72a9b0e2ff` in
[`Cerebellum-Lab/reachAQ-hardware`](https://github.com/Cerebellum-Lab/reachAQ-hardware).
It adds acquisition-confirmation outputs for pellet-board tones:

| Pellet-board signal | Firmware behavior |
| --- | --- |
| `STIM0` | Active for the complete 5 kHz Tone 1 interval; inactive otherwise |
| `STIM1` | Active for the complete 6 kHz Tone 2 interval; inactive otherwise |
| `STIM2` | No automatic tone assignment; remains a generic commandable output |
| `STIM3` | No automatic tone assignment; remains a generic commandable output |
| `STATUS_OUT` | Host-commanded 12-bit DAC output, 0-3300 mV; its current commanded value is reported over JerryCAN |
| `BUTTON` | Active-low external input on PA0; reported over JerryCAN as `external_button` with the three door inputs |

The automatic markers are exact-frequency mappings. A tone other than 5 or
6 kHz clears both automatic marker outputs. Tone completion, cancellation, and
tone-start failures also clear them. `STATUS_OUT` and `BUTTON` were not changed
by v2.0.0 and do not autonomously start a training action.

## Process overview

```text
source change -> commit -> version tag -> build rig -> release bundle
                                                        |
                                  +---------------------+--------------------+
                                  |                     |                    |
                               rig 1 flash           rig 2 flash          rig N flash
```

Every rig must use the same archive and pass the included checksum validation.
Do not rebuild separately on each rig.

## Short command reference

The small command front ends cover routine operation:

```bash
# Designated build rig
tools/reachaq-firmware setup
tools/reachaq-firmware release v2.0.1

# Flash-only rig, from inside an extracted release bundle
./reachaq-update --2.0.1
```

Additional build-rig commands are `reachaq-firmware build vX.Y.Z` for a
build-only verification and `reachaq-firmware package vX.Y.Z` to package
already-built outputs. The longer scripts remain available for diagnosis and
as stable implementation entry points.

## Build rig: one-time setup

The supported reference host is Ubuntu 22.04 on x86-64. Clone only the
Cerebellum Lab fork:

```bash
cd ~/Documents
git clone git@github.com:Cerebellum-Lab/reachAQ-hardware.git
cd reachAQ-hardware
```

Run the idempotent setup helper:

```bash
tools/reachaq-firmware setup
```

It performs the complete setup that otherwise has to be done manually:

1. installs the Ubuntu build packages, excluding `gcc-multilib` and
   `g++-multilib` because they are unnecessary for the ARM SDK and can conflict
   with workstation compiler PPAs;
2. creates `firmware/.venv`;
3. installs West, the firmware Python requirements, and the repository-tested
   CMake 3.31.2 in that virtual environment;
4. runs `west init -l firmware` when required and then `west update`;
5. downloads Zephyr SDK 0.16.8 and installs only the ARM toolchain and host
   tools; and
6. writes the local SDK path to the ignored `.build-host.env` file.

The first setup downloads more than 1 GB of Zephyr dependencies. It is normal
for output to pause during large Git checkouts.

To keep existing system packages unchanged on an already prepared host:

```bash
tools/reachaq-firmware setup --skip-system-packages
```

## Build rig: make a release

Firmware versions are generated from Git tags. Never manually edit the ignored
`firmware/pellet_module/VERSION` file.

For each release:

1. Make and review the firmware changes.
2. Update release documentation and commit all tracked changes.
3. Create an annotated semantic-version tag on that commit.
4. Build and package while `HEAD` is exactly at that tag.
5. Push the branch and tag only to the Cerebellum Lab fork.
6. Attach the resulting archive and archive checksum to a GitHub release.

Example for a future `v2.0.1` release:

```bash
cd ~/Documents/reachAQ-hardware
git status --short
git add <reviewed-files>
git commit -m "Describe the pellet firmware change"
git tag -a v2.0.1 -m "Cerebellum Lab pellet firmware 2.0.1"

tools/reachaq-firmware release v2.0.1

git push origin main
git push origin v2.0.1
```

The release helper performs a pristine pellet firmware build, builds the host
updater, verifies that `APP_VERSION_STRING` equals the tag, and creates:

```text
dist/reachaq-pellet-v2.0.1-linux-x86_64.tar.gz
dist/reachaq-pellet-v2.0.1-linux-x86_64.tar.gz.sha256
```

The archive contains:

```text
reachaq-pellet-v2.0.1-linux-x86_64/
├── flash_pellet_module.sh
├── jerrycan_updater_v2.0.1_linux_x86_64
├── pellet_module_fw_v2.0.1.bin
├── reachaq-update
├── RELEASE-MANIFEST.txt
└── SHA256SUMS
```

To publish with GitHub CLI after reviewing the artifacts:

```bash
gh release create v2.0.1 \
  dist/reachaq-pellet-v2.0.1-linux-x86_64.tar.gz \
  dist/reachaq-pellet-v2.0.1-linux-x86_64.tar.gz.sha256 \
  --repo Cerebellum-Lab/reachAQ-hardware \
  --title "Pellet firmware v2.0.1" \
  --generate-notes
```

The first updater build can appear quiet for 5-10 minutes while CMake downloads
Boost and its submodules. Later builds reuse `build/_deps` and are much faster.

### Direct build commands

The helper scripts are the canonical workflow. For diagnosis, their essential
commands are:

```bash
export PATH="$PWD/firmware/.venv/bin:$PATH"
source .build-host.env

west build \
  --build-dir firmware/pellet_module/build \
  --sysbuild \
  --board cerebellumlab_pellet_module \
  --pristine always \
  firmware/pellet_module

cmake -S . -B build -GNinja -DCMAKE_INSTALL_PREFIX="$PWD/tmp"
cmake --build build --target jerrycan_updater
```

Zephyr always calls its generated signed artifact `zephyr.signed.bin`. The
release helper copies that file to the unambiguous release name
`pellet_module_fw_vX.Y.Z.bin`; renaming does not change the embedded version or
signature.

## Flash-only rigs: prerequisites

A flash-only rig needs:

- a Linux machine matching the updater architecture in the bundle;
- the pellet board powered and connected to the rig's CAN adapter;
- SocketCAN interface `can0` configured as CAN FD with 1,000,000 bit/s nominal
  and 5,000,000 bit/s data rate; and
- the release `.tar.gz` and matching `.tar.gz.sha256` from the designated build
  rig or GitHub release.

The normal reachAQ installation provides `reachaq-can.service`. Confirm that it
has configured the interface:

```bash
systemctl is-active reachaq-can.service
ip -details link show can0
```

Keep `reachaq-can.service` running during the update. Close the reachAQ
application itself so it does not consume CAN responses intended for the
updater.

## Flash-only rigs: one-command update

Verify and extract the release archive:

```bash
sha256sum --check reachaq-pellet-v2.0.1-linux-x86_64.tar.gz.sha256
tar -xzf reachaq-pellet-v2.0.1-linux-x86_64.tar.gz
cd reachaq-pellet-v2.0.1-linux-x86_64
```

For the standard pellet-board JerryCAN address `0`, run:

```bash
./reachaq-update --2.0.1
```

The command verifies that the requested version exists, then the underlying
flash helper verifies every file in the bundle, checks the `can0` bitrate and
CAN FD state, displays the updater version, and asks the operator to type
`FLASH` before changing the board. If a rig uses another address:

```bash
./reachaq-update --2.0.1 --address <decimal-address>
```

Keep power and CAN connected until `JerryCAN update complete` appears. The
updater sends 3,798 acknowledged 64-byte blocks for the v2.0.0 image, writes
them to flash, waits for reboot, verifies the running application, and finalizes
the image. A quiet 40-60 second transfer is expected.

## Determine an unknown board address

If reachAQ is installed, discovery is non-motion and can be run before closing
the application:

```bash
cd ~/Documents/reachAQ
conda run -n reachaq python tools/hardware/validate_can_hardware.py \
  --transport socketcan \
  --channel can0 \
  --action discover
```

Do not run this command while the firmware build virtual environment is active.
If the prompt begins with `(.venv)`, first run:

```bash
deactivate
```

The standard Cerebellum Lab pellet board discovered during v2.0.0 deployment at
address `0`.

## Post-update verification on every rig

Run reachAQ discovery and the firmware-version request:

```bash
cd ~/Documents/reachAQ
conda run -n reachaq python tools/hardware/validate_can_hardware.py \
  --transport socketcan --channel can0 --action discover
conda run -n reachaq python tools/hardware/validate_can_hardware.py \
  --transport socketcan --channel can0 --action version
```

Then perform the acquisition acceptance check:

1. Confirm both marker lines are inactive with no tone playing.
2. Play Tone 1 at 5 kHz and confirm only `STIM0` is active for the same interval.
3. Play Tone 2 at 6 kHz and confirm only `STIM1` is active for the same interval.
4. Stop/abort each tone and confirm its marker returns inactive.
5. Record the release version and archive SHA-256 in the rig maintenance log.

## Troubleshooting

### `CMake is not installed`

Run `tools/setup_firmware_build_host.sh`. Ubuntu's CMake 3.22 can build Zephyr,
but `libjerrycan` requires at least 3.25; the setup helper installs the tested
3.31.2 executable inside `firmware/.venv` and the build helper uses it directly.

### Apt reports `gcc-11-multilib` dependency conflicts

Do not install `gcc-multilib` or `g++-multilib` for this workflow. They are not
needed by the Zephyr ARM SDK. The setup helper deliberately excludes them.

### CMake pauses after `Looking for fwrite_unlocked - found`

On the first updater build, CMake is cloning Boost 1.86 and many submodules. It
can consume about 1 GB and remain quiet for several minutes. Let it finish.

### `ModuleNotFoundError: No module named 'humps'`

The firmware `.venv` is nested over the reachAQ Conda environment. Run
`deactivate` so `(.venv)` disappears, then repeat `conda run -n reachaq ...`.
`pyhumps` is already part of the reachAQ environment.

### The updater appears frozen while sending

The updater waits for a flash-write acknowledgment after every 64-byte CAN-FD
block and intentionally waits at least 10 seconds during reboot. It prints no
percentage progress. Do not interrupt it unless it reports an error or has been
inactive far beyond the normal 40-60 second window.

### Recovery boundary

The release bundle updates the signed application through the existing
bootloader. It does not replace MCUBoot. If the board no longer responds to
JerryCAN discovery or the updater cannot reach the bootloader, use the hardware
debug/programming interface and the Zephyr `west flash` recovery process on the
designated build rig.
