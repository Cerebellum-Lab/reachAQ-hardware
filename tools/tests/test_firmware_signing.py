"""Tests for tools/firmware_signing.py. Run: python -m unittest discover tools/tests"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import firmware_signing as fs  # noqa: E402


class SigningKeyTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        # A system temp directory is not inside a git work tree.
        self.assertIsNone(fs.enclosing_repository(self.tmp))

    def tearDown(self):
        self._tmp.cleanup()

    def _registry(self, fingerprint, status="active", board="pellet_module"):
        path = self.tmp / "registry.json"
        path.write_text(json.dumps({"schema_version": 1, "keys": [{
            "id": "pellet-k9", "board": board, "algorithm": "rsa-2048",
            "fingerprint_sha256": fingerprint, "status": status, "reason": "test"}]}))
        return path

    def _generate(self):
        return fs.generate(self.tmp / "keys", 9999, passphrase=b"test passphrase only")

    def test_generate_writes_encrypted_private_key_and_public_key(self):
        private_path, public_path, fingerprint = self._generate()
        private_text = private_path.read_text()
        self.assertIn("BEGIN ENCRYPTED PRIVATE KEY", private_text)
        self.assertEqual(fs.pem_kind(public_path.read_bytes()), "public")
        self.assertEqual(fs.fingerprint_of(fs.load_public_key(public_path)), fingerprint)

    def test_generate_refuses_a_directory_inside_a_repository(self):
        with self.assertRaises(fs.SigningError):
            fs.generate(fs.REPO_ROOT / "tmp-keys", 9999, passphrase=b"test passphrase only")
        self.assertFalse((fs.REPO_ROOT / "tmp-keys").exists())

    def test_generate_refuses_to_overwrite(self):
        self._generate()
        with self.assertRaises(fs.SigningError):
            self._generate()

    def test_check_public_accepts_only_an_active_registered_key(self):
        _, public_path, fingerprint = self._generate()
        self.assertEqual(fs.check_public(public_path, self._registry(fingerprint))["id"], "pellet-k9")
        for registry in (self._registry(fingerprint, status="revoked"),
                         self._registry(fingerprint, board="magnet_module"),
                         self._registry("sha256:" + "0" * 64)):
            with self.assertRaises(fs.SigningError):
                fs.check_public(public_path, registry)

    def test_check_public_refuses_a_private_key(self):
        private_path, _, fingerprint = self._generate()
        with self.assertRaises(fs.SigningError) as raised:
            fs.check_public(private_path, self._registry(fingerprint))
        self.assertIn("private key", str(raised.exception))

    def test_check_public_requires_an_absolute_path(self):
        with self.assertRaises(fs.SigningError):
            fs.check_public(Path("relative.pem"), self._registry("x"))

    def test_registry_revokes_the_committed_pellet_key(self):
        entries = {entry["id"]: entry for entry in fs.load_registry()}
        self.assertEqual(entries["pellet-k1"]["status"], "revoked")
        self.assertFalse(any(entry["status"] == "active" and entry["id"] == "pellet-k1"
                             for entry in entries.values()))

    def test_revoked_key_is_refused_by_the_real_registry(self):
        # Rebuild the revoked key's public half from history to prove the
        # registry fingerprint matches it. Only the public key is written.
        old = subprocess.run(
            ["git", "-C", str(fs.REPO_ROOT), "show",
             "ff2a61b:firmware/pellet_module/pellet_module_key.pem"],
            capture_output=True, check=False)
        if old.returncode != 0:
            self.skipTest("revoked key commit not available in this clone")
        serialization, _ = fs._crypto()
        key = serialization.load_pem_private_key(old.stdout, password=None)
        public_path = self.tmp / "old.public.pem"
        public_path.write_bytes(key.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        with self.assertRaises(fs.SigningError) as raised:
            fs.check_public(public_path)
        self.assertIn("pellet-k1 is revoked", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
