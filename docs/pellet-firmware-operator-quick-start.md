# Pellet firmware update: operator quick start

Use this page when updating an existing reachAQ rig. You do not need firmware
build tools or this source repository.

## Before starting

- Use the release archive assigned to all rigs by the lab maintainer.
- Update reachAQ first with `reachaq-sync`. reachAQ only accepts firmware
  versions it lists, so a board flashed ahead of it is refused at startup; the
  updater checks this and will not flash a version this rig's reachAQ does not
  accept.
- Keep the pellet board powered and connected to CAN.
- Close the reachAQ application.
- Leave `reachaq-can.service` running.

## Update the rig

Replace `X.Y.Z` with the assigned release, such as `2.0.0`:

```bash
sha256sum --check reachaq-pellet-vX.Y.Z-linux-x86_64.tar.gz.sha256
tar -xzf reachaq-pellet-vX.Y.Z-linux-x86_64.tar.gz
cd reachaq-pellet-vX.Y.Z-linux-x86_64
./reachaq-update --X.Y.Z
```

Before the prompt, the banner's `reachAQ:` line says whether this rig's reachAQ
accepts the version. If the updater refuses instead, run `reachaq-sync` and try
again; do not override it unless the lab maintainer asks you to.

Type `FLASH` when prompted. Do not disconnect power or CAN. A quiet period of
40-60 seconds is normal. Wait for:

```text
JerryCAN update complete
```

## After updating

Start reachAQ (`reachaq`, or the desktop icon) and confirm the pellet
controller connects: the hardware refresh shows the controller connected with
the new firmware version. Test that Tone 1 appears on `STIM0` and Tone 2
appears on `STIM1` in the acquisition system.

If the command reports an error, save the complete terminal output and stop.
Do not try a different firmware version or interrupt board power during an
active transfer.

Developer setup, release creation, recovery, and troubleshooting are in the
[full firmware guide](pellet-firmware-release-and-deployment.md).
