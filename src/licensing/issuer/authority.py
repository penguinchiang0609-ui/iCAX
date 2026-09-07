"""Offline signing primitives. Never include this directory in the client package.

The CLI intentionally does not offer production issuance until enrollment
attestation validation is implemented. Signing primitives are tested separately.
"""
from __future__ import annotations

import argparse
import getpass
import os
from pathlib import Path
import struct

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

MAGIC = b"TDLIC001"
PRODUCT = "icax.tube-designer"
P256_PUBLIC_MAGIC = 0x31534345


def exclusive_write(path: Path, data: bytes) -> None:
    """Refuse overwrite. Operator manages permissions on the offline volume."""
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def initialize_key(path: Path, password: bytes) -> None:
    if len(password) < 16:
        raise ValueError("Use a strong passphrase of at least 16 UTF-8 bytes")
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.private_bytes(serialization.Encoding.PEM,
                            serialization.PrivateFormat.PKCS8,
                            serialization.BestAvailableEncryption(password))
    exclusive_write(path, pem)


def load_private_key(path: Path, password: bytes) -> ec.EllipticCurvePrivateKey:
    with path.open("rb") as stream:
        pem = stream.read(16385)
    if len(pem) > 16384 or not pem.startswith(b"-----BEGIN ENCRYPTED PRIVATE KEY-----"):
        raise ValueError("An encrypted PKCS#8 signing key is required")
    key = serialization.load_pem_private_key(pem, password=password)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Expected P-256 issuer key")
    return key


def public_blob(key: ec.EllipticCurvePublicKey) -> bytes:
    if not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Expected P-256 public key")
    numbers = key.public_numbers()
    return struct.pack("<II", P256_PUBLIC_MAGIC, 32) + numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")


def import_public_blob(blob: bytes) -> ec.EllipticCurvePublicKey:
    if len(blob) != 72 or blob[:8] != struct.pack("<II", P256_PUBLIC_MAGIC, 32):
        raise ValueError("Invalid public key blob")
    return ec.EllipticCurvePublicNumbers(int.from_bytes(blob[8:40], "big"),
                                        int.from_bytes(blob[40:72], "big"), ec.SECP256R1()).public_key()


def identifier(value: str) -> bytes:
    data = value.encode("ascii")
    if not 1 <= len(data) <= 128 or any(byte < 33 or byte > 126 for byte in data):
        raise ValueError("Invalid protocol identifier")
    return struct.pack(">I", len(data)) + data


def encode_body(*, issuer_id: str, license_id: str, request_id: str, customer_id: str,
                device_public_key: bytes, kind: int, features: int, min_major: int,
                max_major: int, issued_at: int, not_before: int = 0, expires_at: int = 0,
                strategy: int = 1, trial_nv_public: bytes = b"", trial_initial_counter: int = 0,
                trial_quantum_seconds: int = 0) -> bytes:
    import_public_blob(device_public_key)
    values = (kind, features, min_major, max_major, issued_at, not_before, expires_at, strategy)
    if any(type(value) is not int for value in values):
        raise ValueError("Protocol numbers must be integers, not booleans or floats")
    if strategy not in (1, 2) or kind not in (1, 2) or not 0 < features <= 15:
        raise ValueError("Invalid strategy, kind or features")
    if not 0 <= min_major <= max_major <= 0xffffffff or not 0 < issued_at <= 0xffffffffffffffff:
        raise ValueError("Invalid version range or issue date")
    if kind == 1 and (not_before != 0 or expires_at != 0):
        raise ValueError("Permanent licenses do not depend on the wall clock")
    if kind == 2 and not issued_at <= not_before < expires_at <= 0xffffffffffffffff:
        raise ValueError("Invalid trial dates")
    body = MAGIC + b"".join(identifier(value) for value in
                            (issuer_id, license_id, request_id, customer_id, PRODUCT))
    body += struct.pack(">IIIIIQQQ", strategy, kind, features, min_major, max_major,
                        issued_at, not_before, expires_at)
    body += struct.pack(">I", len(device_public_key)) + device_public_key
    if trial_nv_public:
        if (kind != 2 or strategy != 1 or len(trial_nv_public) != 14
            or type(trial_initial_counter) is not int or not 0 < trial_initial_counter <= 0xffffffffffffffff
            or type(trial_quantum_seconds) is not int or trial_quantum_seconds != 3600):
            raise ValueError("Invalid NV trial binding")
        body = b"TDLIC002" + body[8:] + struct.pack(">I", len(trial_nv_public)) + trial_nv_public
        body += struct.pack(">QI", trial_initial_counter, trial_quantum_seconds)
    elif trial_initial_counter or trial_quantum_seconds:
        raise ValueError("Incomplete trial binding")
    return body


def sign_body(key: ec.EllipticCurvePrivateKey, body: bytes) -> bytes:
    """Low-level primitive, NOT an enrollment approval or public issuance endpoint."""
    if not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Expected P-256 signing key")
    if body[:8] not in (MAGIC, b"TDLIC002") or len(body) > 8192 - 64:
        raise ValueError("Invalid certificate body")
    signature = key.sign(body, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(signature)
    return body + r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_signature(public_key: bytes, certificate: bytes) -> None:
    if not 64 < len(certificate) <= 8192:
        raise ValueError("Invalid certificate size")
    r = int.from_bytes(certificate[-64:-32], "big")
    s = int.from_bytes(certificate[-32:], "big")
    import_public_blob(public_key).verify(utils.encode_dss_signature(r, s),
                                         certificate[:-64], ec.ECDSA(hashes.SHA256()))


def main() -> None:
    parser = argparse.ArgumentParser(description="Offline issuer key preparation; production issuance is not yet enabled")
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init-key", help="Run ONLY on your isolated signing computer")
    init.add_argument("key", type=Path)
    export = sub.add_parser("export-public", help="Export only a public key for a future client build")
    export.add_argument("key", type=Path)
    export.add_argument("output", type=Path)
    args = parser.parse_args()
    password = getpass.getpass("Signing key passphrase (never passed on command line): ").encode("utf-8")
    if args.command == "init-key":
        confirm = getpass.getpass("Confirm passphrase: ").encode("utf-8")
        if password != confirm:
            raise ValueError("Passphrases do not match")
        initialize_key(args.key, password)
        print("Encrypted signing key created. Keep it offline; make a protected offline backup.")
    else:
        key = load_private_key(args.key, password)
        exclusive_write(args.output, public_blob(key.public_key()))
        print("Public key exported. No private key material was exported.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from None
