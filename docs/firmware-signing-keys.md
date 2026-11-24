# Pellet firmware signing keys

MCUboot on the pellet board boots an application image only if it carries a
valid RSA-2048 signature from the key embedded in the bootloader. This page
covers who holds that key, how builds and releases use it, and how to replace
it.

## Why the keys changed

Until 2026-11-23 the signing keys were committed to this repository as
`firmware/pellet_module/pellet_module_key.pem` and
`firmware/magnet_module/magnet_module_key.pem` (commit `ff2a61b`,
2024-11-22). Both files are RSA-2048 **private** keys, published in this public
repository and its public upstream. Anyone can sign firmware that a bootloader
built from them will accept. Removing them from history would not change that,
so they are revoked instead:

| Key id | Board | Fingerprint (SHA-256 of DER public key) | Status |
| --- | --- | --- | --- |
| `pellet-k1` | pellet_module | `c969f75d179fe7a3dbc1d54b63a424a6c1d5bdfd3bf30316e00e04fbbbcb1142` | revoked 2026-11-23 |
| `magnet-k1` | magnet_module | `a4ce9ace5526c270344741668ba8ec2670874920f923239946fa5e3b760b3283` | compromised, still in use |

The pellet key file is deleted from the tree, and the build refuses
`pellet-k1`. A board keeps trusting `pellet-k1` until its MCUboot is reflashed
over SWD; see [pellet-rekey-reflash-plan.md](pellet-rekey-reflash-plan.md).
The magnet module is not used by reachAQ and still builds with its committed
key. Do not ship magnet boards until that key is replaced the same way.

## How it works now

- **No key in the repository.** `firmware/signing_keys.json` lists keys by
  public fingerprint and status (`active`, `revoked`, `compromised`). Only
  fingerprints go there. `*.private.pem` is ignored by git.
- **MCUboot gets the public key as a build input.** The build is given
  `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="<absolute path to pellet-kN.public.pem>"`.
  `firmware/pellet_module/sysbuild.cmake` stops the build unless that file is
  a public key listed as `active`. A private key, a revoked key, an
  unregistered key, or no key (which would fall back to MCUboot's public
  development key) all fail the configure step.
- **The application builds unsigned.** `zephyr.signed.bin` straight out of the
  build carries only a hash and will not boot.
- **Signing is a separate step** with the private key read from outside every
  repository: `tools/firmware_signing.py sign` runs `west sign` with the
  build's own header, slot and version parameters, then verifies both signed
  images against the public key that this build's MCUboot embeds.
- **Packaging verifies again.** `tools/build_pellet_release.sh` refuses to
  package an image that does not verify against an active key, and records the
  key id and fingerprint in `RELEASE-MANIFEST.txt`.

The host (reachAQ) never sees keys. Its firmware gate is an exact version
allowlist, so every re-keyed image must ship under a new version number.

## Key custody

| Item | Where |
| --- | --- |
| Private key, primary | Passphrase-encrypted PEM on a dedicated offline USB stick, mounted only to sign a release |
| Private key, backup | Second encrypted copy held by Ben Reynolds at a separate site |
| Passphrase | Held by Ben Reynolds, not stored with either copy |
| Public key | Any build host, e.g. `~/.config/reachaq/signing/pellet-k2.public.pem` (not secret) |
| Fingerprint registry | `firmware/signing_keys.json` (committed) |

Never put the private key in a repository, in a repository folder on any
machine, in chat, in an issue or in CI secrets. Never decrypt it to disk.

Keys are named `pellet-kN` by generation. They are replaced only on compromise
or change of ownership. Losing both private-key copies has the same cost as a
compromise: every board needs an SWD visit.

## Generate a new key (one-time, Ben)

On a trusted machine with the firmware virtual environment, mount the signing
USB, then:

```bash
tools/reachaq-firmware keygen --out-dir /media/$USER/SIGNING/pellet --generation 2
```

The tool refuses any directory inside a git work tree and never overwrites a
file. It asks for a passphrase (at least 12 characters) twice and writes:

```text
pellet-k2.private.pem   encrypted PKCS#8, owner-only permissions
pellet-k2.public.pem    public key
```

It prints the public fingerprint only. Then:

1. Copy `pellet-k2.private.pem` to the backup medium and store it at the second
   site.
2. Add the key to `firmware/signing_keys.json`:

   ```json
   {
     "id": "pellet-k2",
     "board": "pellet_module",
     "algorithm": "rsa-2048",
     "fingerprint_sha256": "sha256:<printed fingerprint>",
     "status": "active",
     "created_on": "YYYY-MM-DD",
     "reason": "Replaces pellet-k1, whose private key was public."
   }
   ```

3. Copy `pellet-k2.public.pem` to each build host and point
   `REACHAQ_PELLET_SIGNING_PUBKEY` at it. Check it with
   `tools/reachaq-firmware verify` after the first signed build, or
   `python tools/firmware_signing.py check-public <path>` right away.

## Build, sign and release

Development build (unsigned application, MCUboot with the active key):

```bash
cd firmware/pellet_module
west build --sysbuild --board cerebellumlab_pellet_module -p always -- \
  -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$REACHAQ_PELLET_SIGNING_PUBKEY\"
```

The quotes around the path are part of the value; Kconfig strings need them.

Sign it (mount the signing USB first; imgtool asks for the passphrase once per
image format, so twice):

```bash
tools/reachaq-firmware sign --build-dir firmware/pellet_module/build \
  --key /media/$USER/SIGNING/pellet/pellet-k2.private.pem
```

Release, from a clean tree with HEAD tagged:

```bash
export REACHAQ_PELLET_SIGNING_PUBKEY=~/.config/reachaq/signing/pellet-k2.public.pem
tools/reachaq-firmware release \
  --signing-key /media/$USER/SIGNING/pellet/pellet-k2.private.pem vX.Y.Z
```

Unmount the USB when signing is done. `reachaq-firmware build vX.Y.Z` needs
only the public key and produces an unsigned build for checking;
`reachaq-firmware package vX.Y.Z` verifies the signature before packaging.

## Moving to a cloud KMS or HSM later

The bootloader does not change: it only holds the public key. imgtool 2.1.0
(the MCUboot version in `deps/`) can sign with an external signer. `west sign`
passes everything after `--` to imgtool, so the flow is:

1. `west sign ... -- --vector-to-sign payload` writes the bytes to sign instead
   of signing.
2. The KMS signs those bytes with RSA-2048 RSASSA-PSS, SHA-256, MGF1-SHA-256
   and a 32-byte salt. That is the only RSA mode MCUboot verifies, and what
   imgtool itself produces.
3. `west sign ... -- --fix-sig <signature file> --fix-sig-pubkey <public key>`
   inserts the signature.
4. `tools/firmware_signing.py verify` checks the result as before.

Only `sign_build` in `tools/firmware_signing.py` needs a second backend for
this.

## Replacing a key

1. Generate `pellet-k(N+1)` as above. Mark the old key `revoked` in
   `firmware/signing_keys.json`, with the date and reason.
2. Release a new firmware version signed with the new key.
3. Reflash MCUboot and the new application over SWD on every board. CAN
   updates cannot do this: the old bootloader rejects images signed with the
   new key, and MCUboot does not update itself.
4. Qualify the version on the host before the boards return to service.
