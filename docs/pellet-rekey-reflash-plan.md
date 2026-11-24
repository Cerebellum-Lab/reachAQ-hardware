# Pellet board re-key: reflash and qualification plan

Moves every pellet board from the revoked `pellet-k1` key to `pellet-k2`. Key
handling is in [firmware-signing-keys.md](firmware-signing-keys.md).

## What has to happen and why

- MCUboot holds the public key, and MCUboot does not update itself. A CAN
  update cannot change the key: the old bootloader rejects `pellet-k2` images.
  **Every board needs one SWD visit** (STLINK-V3MINIE + TC2070-NL, as in
  `firmware/WINDOWS_SETUP_AND_FLASHING.md` section 6) to flash the new MCUboot
  and a `pellet-k2` application together.
- After that visit, CAN updates work as before, but only with `pellet-k2`
  images. A board that has not been reflashed still accepts anything signed
  with the public `pellet-k1` key. It must not ship.
- reachAQ's firmware gate is an exact version allowlist and cannot see keys.
  The re-keyed firmware must therefore carry a **new version number**, never
  an existing one re-signed, so the host and the qualification records can tell
  re-keyed boards apart.

## Boards

| Board | Location | Now | Target |
| --- | --- | --- | --- |
| Rig pellet board | christielab10 | MCUboot `pellet-k1`; last qualification record 2.3.1 (2026-11-10). Read the version at the rig before starting. | MCUboot `pellet-k2` + re-key release |
| Bench and spare pellet boards | Ben's bench | `pellet-k1` | Same, before any leaves the lab |
| Any pellet board elsewhere (other rigs, collaborators) | List them first | `pellet-k1` | Same, or recall it |

Record each board when done: serial or label, date, MCUboot key id, firmware
version, who flashed it.

## 0. Before anyone goes to the rig

1. Ben generates `pellet-k2`, stores the backup at the second site and registers
   the fingerprint in `firmware/signing_keys.json`
   ([firmware-signing-keys.md](firmware-signing-keys.md#generate-a-new-key-one-time-ben)).
2. Merge this signing change into the line the next release comes from
   (`integrate/pellet-v2.4.0` or `fix/pellet-v2.3.2`; the touched files are
   identical on both, so it merges cleanly). Pick the re-key version: the next
   unreleased number on that line, for example 2.4.0, or 2.3.2.
3. Build and sign the release on the build host:

   ```bash
   export REACHAQ_PELLET_SIGNING_PUBKEY=~/.config/reachaq/signing/pellet-k2.public.pem
   tools/reachaq-firmware release --signing-key /media/$USER/SIGNING/pellet/pellet-k2.private.pem vX.Y.Z
   ```

   Keep the build directory. SWD needs `firmware/pellet_module/build/mcuboot`
   and the signed `firmware/pellet_module/build/pellet_module`, which the
   CAN-only release bundle does not contain. Record SHA-256 of
   `build/mcuboot/zephyr/zephyr.hex` and `build/pellet_module/zephyr/zephyr.signed.hex`.
4. **Host first.** Add the version to `config/pellet-firmware-compatibility.yaml`
   in reachAQ (feature-dev, then devel) with a limitation noting that it needs
   the `pellet-k2` bootloader. Deploy it so the operator checkout on the rig,
   `~/Documents/reachAQ`, lists it. Run the pre-flight one-liner from reachAQ's
   `docs/acquisition/pellet-firmware-compatibility.md` through
   `conda run -n reachaq` and confirm it prints `~/Documents/reachAQ`. Do not
   flash until it does.

## 1. At the rig (Ben)

Take the rig lock, close the reachAQ application, leave `reachaq-can.service`
running.

```bash
exec 9>/tmp/reachaq-rig-exclusive.lock && flock 9
```

1. **Record the starting state.** Read the running and slot-1 versions with the
   updater (as the release bundle's `reachaq-update` does) and note them.
2. **Back up the internal flash** for rollback, outside every repository
   (STM32G474RE, 512 KiB at 0x08000000; MCUboot at 0x08000000, application at
   0x08010000):

   ```bash
   pyocd cmd -t stm32g474retx -c "savemem 0x08000000 0x80000 ~/pellet-backups/<board>-k1-$(date +%F).bin"
   sha256sum ~/pellet-backups/<board>-k1-*.bin
   ```

3. **Flash MCUboot and the application in one sitting**, board powered, GRIP-14
   seated throughout. Between the two commands the board has a `pellet-k2`
   bootloader and a `pellet-k1` application, and it will not boot.

   ```bash
   cd firmware/pellet_module
   west flash -d build/mcuboot -r pyocd
   west flash -d build/pellet_module -r pyocd
   ```

   `west flash` on `build/pellet_module` writes `zephyr.signed.hex`; that is
   the signed image only because the release step overwrote it. Run
   `tools/reachaq-firmware verify --build-dir firmware/pellet_module/build`
   first if the build directory has been touched since.
4. Remove the programmer and power-cycle the board.
5. **Confirm the board boots the new image.** The updater's version query must
   report the re-key version as running.
6. **Prove CAN updates still work** with the new key: run the new release
   bundle's `./reachaq-update --X.Y.Z` and confirm it completes and the board
   reports X.Y.Z.
7. **Prove revocation** (optional, Ben's call): CAN-flash an old `pellet-k1`
   bundle, such as v2.3.1. The new MCUboot must refuse to boot it, and the
   board must still report X.Y.Z as running. Note what the updater prints.
8. **Qualify through the operator's own path.** Start reachAQ the way the
   operator does (`reachaq` or the desktop icon), confirm the pellet controller
   connects and the compatibility check passes, then run the standard pellet
   qualification (Tone 1 -> STIM0, Tone 2 -> STIM1, moves, delivery) and save
   the record in `~/Autotrainer/pellet_firmware_qualification`.
9. Release the lock (`exec 9>&-`). Update the policy entry's
   `qualification_status` and date in reachAQ, and the board record.

## 2. Rollback

- **Only SWD can roll back across the key change.** The new MCUboot accepts
  only `pellet-k2` images, so no CAN update returns the board to a `pellet-k1`
  build.
- **Back to the previous state:** write the step 2 backup back with
  `pyocd flash -t stm32g474retx --base-address 0x08000000 <backup>.bin`, then
  power-cycle. This restores trust in the revoked key. Use it only to recover a
  board for service while the problem is fixed, not as a shipping state.
- **Back to an older feature set on the new key:** build the older source with
  this signing tooling as a new version, sign it with `pellet-k2`, add it to the
  host policy, and CAN-flash it. MCUboot here has no downgrade protection, so
  that boots. The host's exact allowlist still decides whether reachAQ accepts
  it.
- **Host side:** removing the new policy entry makes reachAQ refuse the board.
  The board itself is unaffected.

## 3. Also affected

- The rig's own hardware checkout `~/Documents/reachAQ-hardware` (detached at
  `059c45d`) and every other worktree still build with the committed
  `pellet-k1`. Once this change is merged, every pellet build needs
  `REACHAQ_PELLET_SIGNING_PUBKEY`, and releases need the signing USB.
- **Magnet module:** same problem (`magnet-k1` is public). It is not used by
  reachAQ and is out of scope. Do not ship magnet boards until it is re-keyed.
- **IO module** (`feat/io-module-firmware`): has no MCUboot, so its firmware
  is not signature-checked at all. Decide before it ships whether it needs
  secure boot.
