import tempfile
import unittest
import subprocess
import os
import struct
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

import authority
from feature_catalog import ALL_FEATURES, FEATURES, BY_ID


class AuthorityTests(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.device = ec.generate_private_key(ec.SECP256R1())
        self.fields = dict(issuer_id="issuer", license_id="license", request_id="request",
                           customer_id="customer", device_public_key=authority.public_blob(self.device.public_key()),
                           kind=1, features=ALL_FEATURES, min_major=0, max_major=1, issued_at=1000)

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
        binding = dict(kind=2, not_before=1000, expires_at=2593000,
                       trial_nv_public=struct.pack(">IHIHH", 0x01501234, 11, 0x22040014, 0, 8),
                       trial_initial_counter=50, trial_quantum_seconds=3600)
        body = authority.encode_body(**(self.fields | binding))
        self.assertEqual(body[:8], b"TDLIC003")
        self.assertEqual(body[-30:], struct.pack(">I", 14) + binding["trial_nv_public"] + struct.pack(">QI", 50, 3600))
        certificate = authority.sign_body(self.key, body)
        authority.verify_signature(authority.public_blob(self.key.public_key()), certificate)

    def test_trial_binding_required_and_permanent_cannot_include_it(self):
        trial = dict(kind=2, not_before=1000, expires_at=2593000,
                     trial_nv_public=bytes(14), trial_initial_counter=50, trial_quantum_seconds=3600)
        for changes in [dict(trial_nv_public=b""), dict(trial_initial_counter=0),
                        dict(trial_initial_counter=True), dict(trial_quantum_seconds=1), dict(strategy=2)]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                authority.encode_body(**(self.fields | trial | changes))
        with self.assertRaises(ValueError):
            authority.encode_body(**(self.fields | dict(trial_nv_public=bytes(14))))

    def test_every_operation_requires_its_parent_page(self):
        for feature in FEATURES:
            mask = feature.bit | (BY_ID[feature.parent].bit if feature.parent else 0)
            with self.subTest(feature=feature.id):
                self.assertEqual(authority.encode_body(**(self.fields | dict(features=mask)))[:8], b"TDLIC003")
                if feature.parent:
                    with self.assertRaises(ValueError):
                        authority.encode_body(**(self.fields | dict(features=feature.bit)))
        for mask in [8192, 0xffffffff, 0x100000000, -1, 1.0]:
            with self.subTest(mask=mask), self.assertRaises(ValueError):
                authority.encode_body(**(self.fields | dict(features=mask)))

    def test_old_certificate_formats_cannot_be_signed(self):
        body = authority.encode_body(**self.fields)
        for magic in [b"TDLIC001", b"TDLIC002"]:
            with self.subTest(magic=magic), self.assertRaises(ValueError):
                authority.sign_body(self.key, magic + body[8:])

    def test_permanent_binary_layout_and_permission_bits(self):
        body = authority.encode_body(**self.fields)
        offset = 8
        for expected in ["issuer", "license", "request", "customer", authority.PRODUCT]:
            length, = struct.unpack_from(">I", body, offset)
            offset += 4
            self.assertEqual(body[offset:offset + length].decode("ascii"), expected)
            offset += length
        self.assertEqual(struct.unpack_from(">IIIIIQQQ", body, offset), (1, 1, ALL_FEATURES, 0, 1, 1000, 0, 0))
        offset += struct.calcsize(">IIIIIQQQ")
        size, = struct.unpack_from(">I", body, offset)
        offset += 4
        self.assertEqual(size, 72)
        self.assertEqual(body[offset:], self.fields["device_public_key"])

    def test_public_key_validation(self):
        for blob in [b"", b"x" * 72, authority.public_blob(self.key.public_key())[:-1]]:
            with self.assertRaises(ValueError):
                authority.import_public_blob(blob)

    def test_native_protocol_interoperability(self):
        binary_directory = Path(os.environ.get("TD_LICENSE_TEST_BINARY_DIR",
            str(Path(__file__).resolve().parents[3] / "Temp/licensing-build/Release")))
        executable = binary_directory / "td-license-tests.exe"
        if not executable.exists():
            self.skipTest("Build the native Release tests first")
        with tempfile.TemporaryDirectory(prefix="td-protocol-test-") as directory:
            cert_path = Path(directory) / "test.cert"
            pub_path = Path(directory) / "test.pub"
            pub_path.write_bytes(authority.public_blob(self.key.public_key()))
            command = [str(executable), "--verify-certificate", str(cert_path), str(pub_path), "issuer"]
            policies = [dict(features=feature.bit | (BY_ID[feature.parent].bit if feature.parent else 0))
                        for feature in FEATURES]
            policies += [dict(features=ALL_FEATURES), dict(kind=2, not_before=1000, expires_at=2593000,
                         trial_nv_public=struct.pack(">IHIHH", 0x01501234, 11, 0x22040014, 0, 8),
                         trial_initial_counter=50, trial_quantum_seconds=3600)]
            for policy in policies:
                with self.subTest(policy=policy):
                    cert_path.write_bytes(authority.sign_body(self.key, authority.encode_body(**(self.fields | policy))))
                    result = subprocess.run(command, capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip(), "license")
            body = authority.encode_body(**self.fields)
            numeric_offset = 8
            for _ in range(5):
                size, = struct.unpack_from(">I", body, numeric_offset)
                numeric_offset += 4 + size
            invalid_bodies = [magic + body[8:] for magic in (b"TDLIC001", b"TDLIC002")]
            for invalid_features in [16, 128, 1024, 4096, 8192]:
                changed = bytearray(body)
                struct.pack_into(">I", changed, numeric_offset + 8, invalid_features)
                invalid_bodies.append(bytes(changed))
            for invalid_body in invalid_bodies:
                # A valid signature cannot bypass the native format and permission policy.
                r, s = utils.decode_dss_signature(self.key.sign(invalid_body, ec.ECDSA(hashes.SHA256())))
                certificate = invalid_body + r.to_bytes(32, "big") + s.to_bytes(32, "big")
                authority.verify_signature(pub_path.read_bytes(), certificate)
                cert_path.write_bytes(certificate)
                result = subprocess.run(command, capture_output=True, text=True, check=False)
                self.assertNotEqual(result.returncode, 0)
            cert_path.write_bytes(authority.sign_body(self.key, body))
            changed = bytearray(cert_path.read_bytes())
            changed[20] ^= 1
            cert_path.write_bytes(changed)
            self.assertNotEqual(subprocess.run(command, capture_output=True, check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
