import tempfile
import unittest
import subprocess
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric import ec

import authority


class AuthorityTests(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.device = ec.generate_private_key(ec.SECP256R1())
        self.fields = dict(issuer_id="issuer", license_id="license", request_id="request",
                           customer_id="customer", device_public_key=authority.public_blob(self.device.public_key()),
                           kind=1, features=15, min_major=0, max_major=1, issued_at=1000)

    def test_encrypted_key_roundtrip_no_overwrite(self):
        with tempfile.TemporaryDirectory(prefix="td-issuer-test-") as directory:
            path = Path(directory) / "test-only.pem"
            password = b"test-only-not-a-production-secret"
            authority.initialize_key(path, password)
            self.assertTrue(path.read_bytes().startswith(b"-----BEGIN ENCRYPTED PRIVATE KEY-----"))
            key = authority.load_private_key(path, password)
            self.assertEqual(len(authority.public_blob(key.public_key())), 72)
            with self.assertRaises(ValueError):
                authority.load_private_key(path, b"incorrect")
            with self.assertRaises(FileExistsError):
                authority.initialize_key(path, password)

    def test_certificate_signature(self):
        body = authority.encode_body(**self.fields)
        certificate = authority.sign_body(self.key, body)
        pub = authority.public_blob(self.key.public_key())
        authority.verify_signature(pub, certificate)
        for index in range(len(certificate)):
            changed = bytearray(certificate)
            changed[index] ^= 1
            with self.assertRaises((InvalidSignature, ValueError)):
                authority.verify_signature(pub, bytes(changed))

    def test_invalid_policy(self):
        for changes in [dict(features=0), dict(features=16), dict(features=True),
                        dict(strategy=3), dict(kind=3), dict(min_major=2),
                        dict(expires_at=2000), dict(issuer_id="bad\nidentifier"),
                        dict(kind=2, not_before=1000, expires_at=999)]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                authority.encode_body(**(self.fields | changes))

    def test_trial_fixed_dates(self):
        certificate = authority.sign_body(self.key, authority.encode_body(
            **(self.fields | dict(kind=2, not_before=1000, expires_at=2593000))))
        authority.verify_signature(authority.public_blob(self.key.public_key()), certificate)

    def test_public_key_validation(self):
        for blob in [b"", b"x" * 72, authority.public_blob(self.key.public_key())[:-1]]:
            with self.assertRaises(ValueError):
                authority.import_public_blob(blob)

    def test_native_protocol_interoperability(self):
        executable = Path(__file__).resolve().parents[3] / "Temp/licensing-build/Release/td-license-tests.exe"
        if not executable.exists():
            self.skipTest("Build the native Release tests first")
        with tempfile.TemporaryDirectory(prefix="td-protocol-test-") as directory:
            cert_path = Path(directory) / "test.cert"
            pub_path = Path(directory) / "test.pub"
            pub_path.write_bytes(authority.public_blob(self.key.public_key()))
            cert_path.write_bytes(authority.sign_body(self.key, authority.encode_body(**self.fields)))
            command = [str(executable), "--verify-certificate", str(cert_path), str(pub_path), "issuer"]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "license")
            changed = bytearray(cert_path.read_bytes())
            changed[20] ^= 1
            cert_path.write_bytes(changed)
            self.assertNotEqual(subprocess.run(command, capture_output=True, check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
