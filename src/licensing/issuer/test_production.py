from datetime import datetime, timedelta, timezone
import hashlib
import hmac
from pathlib import Path
import sqlite3
import struct
import tempfile
import unittest

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa, padding, utils
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.decrepit.ciphers.modes import CFB
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import authority
from enrollment import parse_request, field
from production import ProductionIssuer
from tpm_credential import Reader, kdfa, sha256_name


def sized(value):
    return struct.pack(">H", len(value)) + value


def f(value):
    return struct.pack(">I", len(value)) + value


class ProductionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="td-production-test-only-")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        tool = Path(__file__).resolve().parents[3] / "Temp/licensing-build/Release/td-license-verify-ek.exe"
        if not tool.exists():
            self.skipTest("Build native EK verifier first")
        class TestAuthority(ProductionIssuer):
            @staticmethod
            def native_verifier():
                return tool
        self.issuer = TestAuthority(self.folder / "test-only-authority")
        self.password = b"TEST-ONLY-NOT-FOR-PRODUCTION"
        self.issuer.initialize(self.password)
        ca_key = ec.generate_private_key(ec.SECP256R1())
        self.ek = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        ca_name = x509.Name([x509.NameAttribute(x509.NameOID.COMMON_NAME, "TEST ONLY ROOT")])
        leaf_name = x509.Name([x509.NameAttribute(x509.NameOID.COMMON_NAME, "TEST SOFTWARE EK")])
        def cert(subject, issuer, pub, ca, signer):
            builder = (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer).public_key(pub)
                .serial_number(x509.random_serial_number()).not_valid_before(datetime.now(timezone.utc) - timedelta(days=1))
                .not_valid_after(datetime.now(timezone.utc) + timedelta(days=1))
                .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)
                .add_extension(x509.KeyUsage(digital_signature=False, content_commitment=False, key_encipherment=not ca,
                    data_encipherment=False, key_agreement=False, key_cert_sign=ca, crl_sign=ca, encipher_only=False, decipher_only=False), critical=True))
            if not ca:
                builder = builder.add_extension(x509.ExtendedKeyUsage([x509.ObjectIdentifier("2.23.133.8.1")]), critical=False)
            return builder.sign(signer, hashes.SHA256()).public_bytes(serialization.Encoding.DER)
        root = cert(ca_name, ca_name, ca_key.public_key(), True, ca_key)
        leaf = cert(leaf_name, ca_name, self.ek.public_key(), False, ca_key)
        root_path = self.folder / "test-root.cer"
        root_path.write_bytes(root)
        self.issuer.add_certificate(root_path, trusted_root=True)
        policy = bytes.fromhex("837197674484b3f81a90cc8d46a5d724fd52d76e06520b64f2a1da1b331469aa")
        ek_area = struct.pack(">HHI", 1, 11, 0x300b2) + sized(policy) + struct.pack(">HHHHHI", 6, 128, 0x43, 0x10, 2048, 0)
        ek_area += sized(self.ek.public_key().public_numbers().n.to_bytes(256, "big"))
        ak = ec.generate_private_key(ec.SECP256R1())
        self.ak = ak
        public = ak.public_key().public_numbers()
        ak_area = struct.pack(">HHI", 0x23, 11, 0x50072) + sized(hashlib.sha256(b"TubeDesigner.AttestationPrimary.v1").digest())
        ak_area += struct.pack(">HHHHH", 0x10, 0x18, 11, 3, 0x10) + sized(public.x.to_bytes(32, "big")) + sized(public.y.to_bytes(32, "big"))
        qualified = b"\0\x0b" + hashlib.sha256(struct.pack(">I", 0x40000001) + sha256_name(ak_area)).digest()
        nonce = b"n" * 32
        attest = struct.pack(">IH", 0xff544347, 0x8018) + sized(qualified) + sized(nonce) + bytes(16) + b"\1" + bytes(8)
        attest += struct.pack(">I", 0) + sized(hashlib.sha256(b"").digest())
        a, b = utils.decode_dss_signature(ak.sign(attest, ec.ECDSA(hashes.SHA256())))
        signature = struct.pack(">HH", 0x18, 11) + sized(a.to_bytes(32, "big")) + sized(b.to_bytes(32, "big"))
        request = b"TDREQ002" + b"".join(f(v) for v in (authority.PRODUCT.encode(), nonce, ak_area, qualified, ek_area))
        request += struct.pack(">I", 1) + f(leaf) + f(attest) + f(signature)
        self.path = self.folder / "test.tdreq"
        self.path.write_bytes(request)
        self.options = dict(customer="测试客户", kind=1, features=15, min_major=1, max_major=2)

    def make_trial_request(self):
        original = self.path.read_bytes()
        request = parse_request(original)
        nv = struct.pack(">IHIHH", 0x01501234, 11, 0x22040014, 0, 8)
        initial = 50
        attest = struct.pack(">IH", 0xff544347, 0x8014) + sized(request.ak_qualified) + sized(request.nonce)
        attest += bytes(16) + b"\1" + bytes(8) + sized(sha256_name(nv)) + b"\0\0" + sized(initial.to_bytes(8, "big"))
        a, b = utils.decode_dss_signature(self.ak.sign(attest, ec.ECDSA(hashes.SHA256())))
        signature = struct.pack(">HH", 0x18, 11) + sized(a.to_bytes(32, "big")) + sized(b.to_bytes(32, "big"))
        self.path.write_bytes(b"TDREQ003" + original[8:] + f(nv) + initial.to_bytes(8, "big") + f(attest) + f(signature))

    def test_verified_issue_and_decrypt(self):
        request, fingerprint = self.issuer.inspect(self.path)
        self.assertEqual(len(fingerprint), 64)
        identifier = self.issuer.issue(self.path, self.password, **self.options)
        output = self.folder / "license.tdact"
        self.issuer.export(identifier, output)
        raw = output.read_bytes()
        pub = (self.issuer.directory / "issuer-public.blob").read_bytes()
        authority.verify_signature(pub, raw)
        r = Reader(raw[:-64]); self.assertEqual(r.take(8), b"TDACT001")
        digest, name, blob, secret, nonce, encrypted = [field(r, 8192) for _ in range(6)]
        r.end()
        seed = self.ek.decrypt(secret, padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=b"IDENTITY\0"))
        self.assertEqual(blob[2:34], hmac.digest(kdfa(seed, b"INTEGRITY", b"", b"", 256), blob[34:] + name, "sha256"))
        decryptor = Cipher(algorithms.AES(kdfa(seed, b"STORAGE", name, b"", 128)), CFB(bytes(16))).decryptor()
        credential = decryptor.update(blob[34:]) + decryptor.finalize()
        self.assertEqual(credential[:2], b"\0 ")
        certificate = AESGCM(credential[2:]).decrypt(nonce, encrypted, digest)
        authority.verify_signature(pub, certificate)
        self.assertIn(request.device_public, certificate)
        with self.assertRaises(sqlite3.IntegrityError):
            self.issuer.issue(self.path, self.password, **self.options)

    def test_no_trusted_root_rejected(self):
        # Move only this test's root to a sibling; never alter the system trust store.
        root = next((self.issuer.directory / "manufacturer-roots").glob("*.cer"))
        root.rename(self.folder / "removed-test-root.cer")
        with self.assertRaises(ValueError):
            self.issuer.inspect(self.path)

    def test_trial_not_silently_downgraded(self):
        with self.assertRaises(ValueError):
            self.issuer.issue(self.path, self.password, **(self.options | dict(kind=2)))
        self.assertEqual(self.issuer.records(), [])

    def test_trial_issuance_requires_counter_and_one_per_device(self):
        self.make_trial_request()
        identifier = self.issuer.issue(self.path, self.password, **(self.options | dict(kind=2)))
        self.assertTrue(identifier)
        # Separate unique guard persists even if the request-id uniqueness row is
        # absent; a new application request must not yield another trial.
        with sqlite3.connect(self.issuer.directory / "issuance.sqlite") as db:
            db.execute("UPDATE issued SET request_hash='other-request'")
            db.commit()
        db.close()
        with self.assertRaises(sqlite3.IntegrityError):
            self.issuer.issue(self.path, self.password, **(self.options | dict(kind=2)))

    def test_request_tamper(self):
        raw = bytearray(self.path.read_bytes()); raw[-1] ^= 1
        with self.assertRaises(Exception):
            parse_request(bytes(raw))

    def test_build_trust_export(self):
        path = self.folder / "IssuerTrust.generated.h"
        self.issuer.export_build_trust(path)
        self.assertIn("array<unsigned char, 72>", path.read_text())
        with self.assertRaises(FileExistsError):
            self.issuer.export_build_trust(path)

    def test_backup_restores_ledger_and_exact_package(self):
        identifier = self.issuer.issue(self.path, self.password, **self.options)
        destination = self.folder / "backup"
        self.issuer.backup(destination)
        restored = ProductionIssuer(destination)
        self.assertEqual(restored.records(), self.issuer.records())
        restored.export(identifier, self.folder / "restored.tdact")
        self.issuer.export(identifier, self.folder / "original.tdact")
        self.assertEqual((self.folder / "restored.tdact").read_bytes(), (self.folder / "original.tdact").read_bytes())
        self.assertFalse((destination / "BACKUP_INCOMPLETE").exists())
        with self.assertRaises(FileExistsError):
            self.issuer.backup(destination)
        with self.assertRaises(ValueError):
            self.issuer.backup(self.issuer.directory / "nested")


if __name__ == "__main__":
    unittest.main()
