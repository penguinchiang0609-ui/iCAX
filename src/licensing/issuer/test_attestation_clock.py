"""Clock-continuity regression with signed, ephemeral software fixtures."""
import hashlib
import os
import struct
import unittest

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

from enrollment import verify_quote, verify_trial_evidence
from tpm_credential import sha256_name


def sized(value):
    return struct.pack(">H", len(value)) + value


class AttestationClockTests(unittest.TestCase):
    def setUp(self):
        self.key = ec.generate_private_key(ec.SECP256R1())
        point = self.key.public_key().public_numbers()
        self.area = struct.pack(">HHI", 0x23, 11, 0x50072)
        self.area += sized(hashlib.sha256(b"TubeDesigner.AttestationPrimary.v1").digest())
        self.area += struct.pack(">HHHHH", 0x10, 0x18, 11, 3, 0x10)
        self.area += sized(point.x.to_bytes(32, "big")) + sized(point.y.to_bytes(32, "big"))
        self.qualified = b"\0\x0b" + hashlib.sha256(struct.pack(">I", 0x40000001) + sha256_name(self.area)).digest()
        self.nonce = os.urandom(32)
        self.nv = struct.pack(">IHIHH", 0x01501234, 11, 0x22040014, 0, 8)

    def sign(self, data):
        a, b = utils.decode_dss_signature(self.key.sign(data, ec.ECDSA(hashes.SHA256())))
        return struct.pack(">HH", 0x18, 11) + sized(a.to_bytes(32, "big")) + sized(b.to_bytes(32, "big"))

    def attest(self, safe, trial):
        data = struct.pack(">IH", 0xff544347, 0x8014 if trial else 0x8018)
        data += sized(self.qualified) + sized(self.nonce)
        data += struct.pack(">QIIBQ", 0, 2, 0, safe, 1)
        if trial:
            data += sized(sha256_name(self.nv)) + b"\0\0" + sized((50).to_bytes(8, "big"))
        else:
            data += struct.pack(">I", 0) + sized(hashlib.sha256(b"").digest())
        return data

    def verify(self, trial, data, signature=None, nonce=None):
        args = (self.area, self.qualified, self.nonce if nonce is None else nonce)
        if trial:
            args += (self.nv, 50)
        args += (data, self.sign(data) if signature is None else signature)
        (verify_trial_evidence if trial else verify_quote)(*args)

    def test_signed_clock_states(self):
        for trial in (False, True):
            for safe in (0, 1):
                with self.subTest(trial=trial, safe=safe):
                    self.verify(trial, self.attest(safe, trial))

    def test_all_malformed_signed_clock_flags(self):
        for trial in (False, True):
            for safe in range(2, 256):
                with self.subTest(trial=trial, safe=safe), self.assertRaisesRegex(ValueError, "clock safe flag"):
                    self.verify(trial, self.attest(safe, trial))

    def test_clock_discontinuity_preserves_freshness(self):
        for trial in (False, True):
            with self.subTest(trial=trial), self.assertRaises(ValueError):
                self.verify(trial, self.attest(0, trial), nonce=os.urandom(32))

    def test_clock_discontinuity_preserves_authentication(self):
        for trial in (False, True):
            data = self.attest(0, trial)
            signature = self.sign(data)
            for offset in range(len(data)):
                changed = bytearray(data)
                changed[offset] ^= 1
                with self.subTest(trial=trial, offset=offset), self.assertRaises(Exception):
                    self.verify(trial, bytes(changed), signature=signature)

    def test_signed_truncations_and_trailing_data(self):
        for trial in (False, True):
            data = self.attest(0, trial)
            for length in range(len(data)):
                with self.subTest(trial=trial, length=length), self.assertRaises(ValueError):
                    self.verify(trial, data[:length])
            with self.subTest(trial=trial), self.assertRaises(ValueError):
                self.verify(trial, data + b"\0")

    def test_clock_discontinuity_preserves_counter_binding(self):
        data = self.attest(0, True)
        with self.assertRaises(ValueError):
            verify_trial_evidence(self.area, self.qualified, self.nonce, self.nv, 51, data, self.sign(data))
        orderly = struct.pack(">IHIHH", 0x01501234, 11, 0x26040014, 0, 8)
        with self.assertRaises(ValueError):
            verify_trial_evidence(self.area, self.qualified, self.nonce, orderly, 50, data, self.sign(data))


if __name__ == "__main__":
    unittest.main()
