"""TPM 2.0 RSA EK credential wrapping (SHA256/AES128-CFB).

The EK must be authenticated by the caller before wrapping a real license.
No private TPM key is read or reconstructed by this module.
"""
import hashlib
import hmac
import secrets
import struct

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.decrepit.ciphers.modes import CFB


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.offset:
            raise ValueError("Truncated TPM data")
        result = self.data[self.offset:self.offset + size]
        self.offset += size
        return result

    def u16(self):
        return int.from_bytes(self.take(2), "big")

    def u32(self):
        return int.from_bytes(self.take(4), "big")

    def sized(self, maximum):
        size = self.u16()
        if size > maximum:
            raise ValueError("TPM field too large")
        return self.take(size)

    def end(self):
        if self.offset != len(self.data):
            raise ValueError("Trailing TPM data")


def sha256_name(public_area):
    return b"\x00\x0b" + hashlib.sha256(public_area).digest()


def rsa_ek(public_area):
    r = Reader(public_area)
    if r.u16() != 1 or r.u16() != 0x0b or r.u32() != 0x300b2:
        raise ValueError("Expected TCG low-range RSA endorsement key")
    expected_policy = bytes.fromhex("837197674484b3f81a90cc8d46a5d724fd52d76e06520b64f2a1da1b331469aa")
    if r.sized(32) != expected_policy:
        raise ValueError("Unexpected EK policy")
    if (r.u16(), r.u16(), r.u16(), r.u16(), r.u16()) != (6, 128, 0x43, 0x10, 2048):
        raise ValueError("Unsupported EK scheme")
    exponent = r.u32() or 65537
    modulus = r.sized(256)
    r.end()
    if len(modulus) != 256 or exponent != 65537 or not modulus[0] & 128:
        raise ValueError("Invalid RSA EK")
    return rsa.RSAPublicNumbers(exponent, int.from_bytes(modulus, "big")).public_key()


def kdfa(key, label, context_u, context_v, bits):
    if bits not in (128, 256):
        raise ValueError("Unsupported KDF length")
    message = struct.pack(">I", 1) + label + b"\x00" + context_u + context_v + struct.pack(">I", bits)
    return hmac.digest(key, message, "sha256")[:bits // 8]


def make_credential(ek_public_area, ak_name, credential):
    if len(ak_name) != 34 or ak_name[:2] != b"\x00\x0b" or len(credential) != 32:
        raise ValueError("Expected SHA256 AK name and 32-byte credential")
    ek = rsa_ek(ek_public_area)
    seed = secrets.token_bytes(32)
    secret = ek.encrypt(seed, padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=b"IDENTITY\x00"))
    storage_key = kdfa(seed, b"STORAGE", ak_name, b"", 128)
    encryptor = Cipher(algorithms.AES(storage_key), CFB(bytes(16))).encryptor()
    encrypted = encryptor.update(struct.pack(">H", len(credential)) + credential) + encryptor.finalize()
    integrity_key = kdfa(seed, b"INTEGRITY", b"", b"", 256)
    integrity = hmac.digest(integrity_key, encrypted + ak_name, "sha256")
    return struct.pack(">H", len(integrity)) + integrity + encrypted, secret


if __name__ == "__main__":
    # Hardware interoperability diagnostic only; expected credential is deliberately
    # included for equality testing. NEVER use this diagnostic container for licenses.
    import argparse
    from pathlib import Path
    import authority
    parser = argparse.ArgumentParser(description="Generate a non-license TPM activation test")
    parser.add_argument("public", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.public.read_bytes()
    if len(data) > 4096:
        raise ValueError("Public bundle too large")
    reader = Reader(data)
    ak = reader.take(reader.u32())
    reader.take(reader.u32())
    ek_area = reader.take(reader.u32())
    reader.end()
    expected = secrets.token_bytes(32)
    blob, secret = make_credential(ek_area, sha256_name(ak), expected)
    authority.exclusive_write(args.output, b"".join(struct.pack(">I", len(v)) + v for v in (blob, secret, expected)))
