#!/usr/bin/env python3
"""Pellet firmware signing keys: generate, check, sign and verify.

The private signing key never lives in a repository. MCUboot is built with
the public key only, the application is built unsigned, and the release image
is signed afterwards with a private key read from outside every repository.

Every public key a build may embed must be listed as active in
firmware/signing_keys.json, by the SHA-256 fingerprint of its DER
SubjectPublicKeyInfo. Revoked keys stay listed so a build refuses them.

Commands:
  generate      Create a new encrypted key pair in a directory outside any repository
  fingerprint   Print the fingerprint of a public key
  check-public  Confirm a public key file is registered and active (used by the build)
  sign          Sign a built pellet image with an external private key, then verify it
  verify        Check a build's signed images against the key its MCUboot embeds

This tool prints public fingerprints only. It never prints, copies or logs
private key material.
"""

from __future__ import annotations

import argparse
import getpass
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
REGISTRY = REPO_ROOT / "firmware" / "signing_keys.json"
IMAGE_NAME = "pellet_module"
BOARD = "pellet_module"
KEY_ID_RE = re.compile(r"^pellet-k(\d+)$")
MIN_PASSPHRASE = 12


class SigningError(Exception):
    pass


def _crypto():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
    except ImportError as exc:  # pragma: no cover - environment problem
        raise SigningError(
            "The 'cryptography' package is required (firmware/requirements.txt)."
        ) from exc
    return serialization, rsa


def enclosing_repository(path: Path) -> Path | None:
    """Return the git work tree containing path, or None."""
    path = path.resolve()
    for candidate in (path, *path.parents):
        if (candidate / ".git").exists():
            return candidate
    return None


def require_outside_repositories(path: Path, what: str) -> None:
    repo = enclosing_repository(path)
    if repo is not None:
        raise SigningError(
            f"{what} must be outside every repository; {path} is inside {repo}."
        )


def pem_kind(data: bytes) -> str:
    text = data.decode("ascii", errors="replace")
    if "PRIVATE KEY-----" in text:
        return "private"
    if "-----BEGIN PUBLIC KEY-----" in text:
        return "public"
    return "unknown"


def load_public_key(path: Path):
    serialization, rsa = _crypto()
    data = path.read_bytes()
    kind = pem_kind(data)
    if kind == "private":
        raise SigningError(
            f"{path} is a private key. Pass the public key; the private key "
            "never enters the firmware build."
        )
    if kind != "public":
        raise SigningError(f"{path} is not a PEM public key.")
    key = serialization.load_pem_public_key(data)
    if not isinstance(key, rsa.RSAPublicKey) or key.key_size != 2048:
        raise SigningError(f"{path} is not an RSA-2048 public key.")
    return key


def fingerprint_of(public_key) -> str:
    serialization, _ = _crypto()
    der = public_key.public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    return "sha256:" + hashlib.sha256(der).hexdigest()


def load_registry(path: Path = REGISTRY) -> list[dict]:
    record = json.loads(path.read_text(encoding="utf-8"))
    if record.get("schema_version") != 1:
        raise SigningError(f"Unsupported signing-key registry schema in {path}.")
    keys = record.get("keys", [])
    ids = [entry["id"] for entry in keys]
    if len(ids) != len(set(ids)):
        raise SigningError(f"Duplicate key ids in {path}.")
    return keys


def check_public(path: Path, registry: Path = REGISTRY) -> dict:
    if not path.is_absolute():
        raise SigningError(f"The public key path must be absolute: {path}")
    if not path.is_file():
        raise SigningError(f"Public key not found: {path}")
    fingerprint = fingerprint_of(load_public_key(path))
    for entry in load_registry(registry):
        if entry.get("fingerprint_sha256") != fingerprint:
            continue
        if entry.get("board") != BOARD:
            raise SigningError(
                f"Key {entry['id']} belongs to {entry.get('board')}, not {BOARD}."
            )
        if entry.get("status") != "active":
            raise SigningError(
                f"Key {entry['id']} is {entry.get('status')}: {entry.get('reason', '')}"
            )
        return entry
    raise SigningError(
        f"Public key {fingerprint} is not in {registry}. Register it as active "
        "before building with it."
    )


def read_passphrase() -> bytes:
    first = getpass.getpass("New key passphrase: ")
    if len(first) < MIN_PASSPHRASE:
        raise SigningError(f"Use a passphrase of at least {MIN_PASSPHRASE} characters.")
    if getpass.getpass("Repeat passphrase: ") != first:
        raise SigningError("Passphrases differ.")
    return first.encode("utf-8")


def generate(out_dir: Path, generation: int, passphrase: bytes | None = None) -> tuple[Path, Path, str]:
    serialization, rsa = _crypto()
    out_dir = out_dir.resolve()
    require_outside_repositories(out_dir, "The key directory")
    key_id = f"pellet-k{generation}"
    for entry in load_registry():
        if entry["id"] == key_id:
            raise SigningError(f"Key id {key_id} is already registered; pick a new generation.")
    private_path = out_dir / f"{key_id}.private.pem"
    public_path = out_dir / f"{key_id}.public.pem"
    for path in (private_path, public_path):
        if path.exists():
            raise SigningError(f"Refusing to overwrite {path}.")
    out_dir.mkdir(parents=True, exist_ok=True)
    if passphrase is None:
        passphrase = read_passphrase()

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    private_pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(passphrase),
    )
    # Create the file with owner-only permissions before any key bytes land in it.
    fd = os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(private_pem)
    public_path.write_bytes(
        key.public_key().public_bytes(
            serialization.Encoding.PEM,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
    )
    return private_path, public_path, fingerprint_of(key.public_key())


def find_imgtool() -> list[str]:
    found = shutil.which("imgtool")
    if found:
        return [found]
    script = REPO_ROOT / "deps" / "bootloader" / "mcuboot" / "scripts" / "imgtool.py"
    if script.is_file():
        return [sys.executable, str(script)]
    raise SigningError("imgtool not found; run tools/setup_firmware_build_host.sh.")


def read_kconfig(config: Path, name: str) -> str:
    if not config.is_file():
        raise SigningError(f"Build configuration not found: {config}")
    pattern = re.compile(rf'^{re.escape(name)}="?(.*?)"?$')
    for line in config.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1)
    raise SigningError(f"{name} is not set in {config}.")


def build_public_key(build_dir: Path) -> Path:
    """The public key this build's MCUboot embeds."""
    return Path(read_kconfig(build_dir / "mcuboot" / "zephyr" / ".config",
                             "CONFIG_BOOT_SIGNATURE_KEY_FILE"))


def signed_images(build_dir: Path) -> tuple[Path, Path]:
    zephyr = build_dir / IMAGE_NAME / "zephyr"
    return zephyr / "zephyr.signed.bin", zephyr / "zephyr.signed.hex"


def verify_image(image: Path, public_key: Path) -> None:
    if not image.is_file():
        raise SigningError(f"Signed image not found: {image}")
    with tempfile.TemporaryDirectory() as scratch:
        target = image
        if image.suffix == ".hex":
            from intelhex import IntelHex

            target = Path(scratch) / "image.bin"
            hexfile = IntelHex(str(image))
            target.write_bytes(hexfile.tobinstr())
        result = subprocess.run(
            [*find_imgtool(), "verify", "-k", str(public_key), str(target)],
            capture_output=True, text=True,
        )
    if result.returncode != 0 or "Image was correctly validated" not in result.stdout:
        raise SigningError(
            f"{image} does not verify against {public_key}: {result.stdout.strip()}"
        )


def verify_build(build_dir: Path) -> dict:
    public_key = build_public_key(build_dir)
    entry = check_public(public_key)
    for image in signed_images(build_dir):
        verify_image(image, public_key)
    return entry


def sign_build(build_dir: Path, private_key: Path) -> dict:
    private_key = private_key.resolve()
    if not private_key.is_file():
        raise SigningError(f"Private key not found: {private_key}")
    require_outside_repositories(private_key, "The private key")
    public_key = build_public_key(build_dir)
    entry = check_public(public_key)
    west = shutil.which("west")
    if not west:
        raise SigningError("west not found; activate firmware/.venv first.")
    signed_bin, signed_hex = signed_images(build_dir)
    # imgtool asks for the passphrase itself, once per output format.
    subprocess.run(
        [west, "sign", "--quiet", "-d", str(build_dir / IMAGE_NAME), "-t", "imgtool",
         "--tool-path", find_imgtool()[-1],
         "--bin", "--sbin", str(signed_bin), "--hex", "--shex", str(signed_hex),
         "--", "--key", str(private_key)],
        check=True,
    )
    verify_build(build_dir)
    return entry


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("generate", help="create a new encrypted pellet key pair")
    p.add_argument("--out-dir", type=Path, required=True,
                   help="directory outside every repository, e.g. the mounted signing USB")
    p.add_argument("--generation", type=int, required=True,
                   help="key generation number; the first replacement key is 2")

    p = sub.add_parser("fingerprint", help="print a public key's fingerprint")
    p.add_argument("public_key", type=Path)

    p = sub.add_parser("check-public", help="confirm a public key is registered and active")
    p.add_argument("public_key", type=Path)

    p = sub.add_parser("sign", help="sign a built pellet image with an external private key")
    p.add_argument("--build-dir", type=Path, required=True)
    p.add_argument("--key", type=Path, required=True, help="private key outside every repository")

    p = sub.add_parser("verify", help="verify a build's signed images")
    p.add_argument("--build-dir", type=Path, required=True)

    args = parser.parse_args(argv)
    try:
        if args.command == "generate":
            private_path, public_path, fingerprint = generate(args.out_dir, args.generation)
            key_id = f"pellet-k{args.generation}"
            print(f"Created {key_id}")
            print(f"  private key (encrypted): {private_path}")
            print(f"  public key:              {public_path}")
            print(f"  fingerprint:             {fingerprint}")
            print("Add this key to firmware/signing_keys.json as active, then copy the")
            print("public key to each build host. Back up the private key file now.")
        elif args.command == "fingerprint":
            print(fingerprint_of(load_public_key(args.public_key)))
        elif args.command == "check-public":
            entry = check_public(args.public_key)
            print(f"{entry['id']} {entry['fingerprint_sha256']}")
        elif args.command == "sign":
            entry = sign_build(args.build_dir, args.key)
            print(f"Signed and verified with {entry['id']}: "
                  f"{', '.join(str(p) for p in signed_images(args.build_dir))}")
        elif args.command == "verify":
            entry = verify_build(args.build_dir)
            print(f"Verified against {entry['id']} {entry['fingerprint_sha256']}")
    except (SigningError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
