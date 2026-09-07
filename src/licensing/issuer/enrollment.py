"""Strict offline TPM enrollment validation; no network or automatic trust import."""
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import struct

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.x509.verification import PolicyBuilder, Store, ExtensionPolicy, Criticality

import authority
from tpm_credential import Reader, rsa_ek, sha256_name


def field(r, maximum):
    size = r.u32()
    if size > maximum:
        raise ValueError("Enrollment field too large")
    return r.take(size)


def ak_public(area):
    r = Reader(area)
    if r.u16() != 0x23 or r.u16() != 0x0b or r.u32() != 0x50072:
        raise ValueError("AK must be TPM-fixed restricted P256 signing key")
    if r.sized(32) != hashlib.sha256(b"TubeDesigner.AttestationPrimary.v1").digest():
        raise ValueError("Unknown AK template")
    if tuple(r.u16() for _ in range(5)) != (0x10, 0x18, 0x0b, 3, 0x10):
        raise ValueError("Unknown AK scheme")
    x, y = r.sized(32), r.sized(32)
    r.end()
    if len(x) != 32 or len(y) != 32:
        raise ValueError("Invalid AK point")
    blob = struct.pack("<II", authority.P256_PUBLIC_MAGIC, 32) + x + y
    authority.import_public_blob(blob)
    return blob


def verify_quote(area, qualified, nonce, attest, signature):
    r = Reader(signature)
    if (r.u16(), r.u16()) != (0x18, 0x0b):
        raise ValueError("Wrong quote signature scheme")
    a, b = r.sized(32), r.sized(32)
    r.end()
    if not a or not b:
        raise ValueError("Empty signature scalar")
    # verify_signature verifies the final 64 bytes over all preceding bytes.
    authority.verify_signature(ak_public(area), attest + a.rjust(32, b"\0") + b.rjust(32, b"\0"))
    expected_qualified = b"\0\x0b" + hashlib.sha256(struct.pack(">I", 0x40000001) + sha256_name(area)).digest()
    if qualified != expected_qualified:
        raise ValueError("AK qualified name does not match primary hierarchy")
    r = Reader(attest)
    if r.u32() != 0xff544347 or r.u16() != 0x8018:
        raise ValueError("Not a TPM quote")
    if r.sized(68) != qualified or r.sized(64) != nonce:
        raise ValueError("Quote binding mismatch")
    r.take(16)
    if r.take(1) != b"\x01":
        raise ValueError("Unsafe TPM clock")
    r.take(8)
    if r.u32() != 0 or r.sized(32) != hashlib.sha256(b"").digest():
        raise ValueError("Unexpected quote PCR data")
    r.end()


def ca_constraints(policy, cert, extension):
    if not extension.ca:
        raise ValueError("Issuer is not a CA")


def ca_usage(policy, cert, extension):
    if not extension.key_cert_sign:
        raise ValueError("CA cannot sign certificates")


def ek_usage(policy, cert, extension):
    if not extension.key_encipherment or extension.key_cert_sign:
        raise ValueError("Invalid endorsement key usage")


def ek_purpose(policy, cert, extension):
    if x509.ObjectIdentifier("2.23.133.8.1") not in extension:
        raise ValueError("Not a TCG endorsement certificate")


def ek_constraints(policy, cert, extension):
    if extension.ca:
        raise ValueError("Endorsement certificate must not be a CA")


def verify_ek(ek_area, certificates, roots, intermediates=(), native_verifier=None):
    if not roots:
        raise ValueError("No independently trusted TPM manufacturer roots configured")
    ek = rsa_ek(ek_area)
    candidates = [x509.load_der_x509_certificate(der) for der in certificates]
    leafs = [cert for cert in candidates if cert.public_key().public_bytes(
        authority.serialization.Encoding.DER, authority.serialization.PublicFormat.SubjectPublicKeyInfo) == ek.public_bytes(
        authority.serialization.Encoding.DER, authority.serialization.PublicFormat.SubjectPublicKeyInfo)]
    if len(leafs) != 1:
        raise ValueError("No unique endorsement certificate matches TPM EK")
    if native_verifier is not None:
        import subprocess
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory(prefix="td-ek-verify-") as directory:
            folder = Path(directory)
            (folder / "roots").mkdir()
            (folder / "intermediates").mkdir()
            (folder / "leaf.cer").write_bytes(leafs[0].public_bytes(authority.serialization.Encoding.DER))
            for i, cert in enumerate(roots):
                (folder / "roots" / f"{i}.cer").write_bytes(cert.public_bytes(authority.serialization.Encoding.DER))
            for i, cert in enumerate([c for c in candidates if c != leafs[0]] + list(intermediates)):
                (folder / "intermediates" / f"{i}.cer").write_bytes(cert.public_bytes(authority.serialization.Encoding.DER))
            result = subprocess.run([str(native_verifier), str(folder / "leaf.cer"), str(folder / "roots"), str(folder / "intermediates")],
                capture_output=True, timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if result.returncode != 0 or not result.stdout.startswith(b"offline_ek_chain=verified\n"):
                # Windows text output may use CRLF.
                if result.returncode != 0 or not result.stdout.startswith(b"offline_ek_chain=verified\r\n"):
                    raise ValueError("Offline EK chain rejected: " + result.stderr.decode("utf-8", errors="replace"))
        return leafs[0].fingerprint(hashes.SHA256()).hex()
    ee = (ExtensionPolicy.permit_all()
          .require_present(x509.BasicConstraints, Criticality.AGNOSTIC, ek_constraints)
          .require_present(x509.KeyUsage, Criticality.AGNOSTIC, ek_usage)
          .require_present(x509.ExtendedKeyUsage, Criticality.AGNOSTIC, ek_purpose))
    ca = (ExtensionPolicy.permit_all()
          .require_present(x509.BasicConstraints, Criticality.CRITICAL, ca_constraints)
          .require_present(x509.KeyUsage, Criticality.AGNOSTIC, ca_usage))
    verifier = (PolicyBuilder().store(Store(list(roots))).time(datetime.now(timezone.utc)).max_chain_depth(5)
                .extension_policies(ee_policy=ee, ca_policy=ca).build_client_verifier())
    result = verifier.verify(leafs[0], [c for c in candidates if c != leafs[0]] + list(intermediates))
    understood = {x509.ExtensionOID.BASIC_CONSTRAINTS, x509.ExtensionOID.KEY_USAGE,
                  x509.ExtensionOID.EXTENDED_KEY_USAGE, x509.ExtensionOID.SUBJECT_ALTERNATIVE_NAME,
                  x509.ExtensionOID.NAME_CONSTRAINTS, x509.ExtensionOID.SUBJECT_KEY_IDENTIFIER,
                  x509.ExtensionOID.AUTHORITY_KEY_IDENTIFIER}
    for cert in result.chain:
        for extension in cert.extensions:
            if extension.critical and extension.oid not in understood:
                raise ValueError("Unsupported critical certificate extension")
    # Revocation is an explicit offline operational responsibility; never claim online checking.
    return leafs[0].fingerprint(hashes.SHA256()).hex()


def verify_ek_native(ek_area, certificates, roots, intermediates, native_verifier):
    """Keep original vendor DER bytes; Windows handles legacy vendor encodings."""
    import subprocess
    import tempfile
    from pathlib import Path
    if not roots:
        raise ValueError("尚未配置独立核验的 TPM 厂商根证书")
    ek = rsa_ek(ek_area).public_bytes(authority.serialization.Encoding.DER, authority.serialization.PublicFormat.SubjectPublicKeyInfo)
    leafs = []
    for der in certificates:
        try:
            cert = x509.load_der_x509_certificate(der)
            if cert.public_key().public_bytes(authority.serialization.Encoding.DER, authority.serialization.PublicFormat.SubjectPublicKeyInfo) == ek:
                leafs.append(der)
        except ValueError:
            continue  # An intermediate's legacy encoding is validated by Windows below.
    if len(leafs) != 1:
        raise ValueError("没有唯一匹配 EK 公钥的厂商叶证书")
    with tempfile.TemporaryDirectory(prefix="td-ek-verify-") as directory:
        folder = Path(directory)
        (folder / "roots").mkdir()
        (folder / "intermediates").mkdir()
        (folder / "leaf.cer").write_bytes(leafs[0])
        for i, der in enumerate(roots):
            (folder / "roots" / f"{i}.cer").write_bytes(der)
        for i, der in enumerate([c for c in certificates if c != leafs[0]] + list(intermediates)):
            (folder / "intermediates" / f"{i}.cer").write_bytes(der)
        result = subprocess.run([str(native_verifier), str(folder / "leaf.cer"), str(folder / "roots"), str(folder / "intermediates")],
            capture_output=True, timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode or b"offline_ek_chain=verified" not in result.stdout.splitlines():
            raise ValueError("厂商证书链验证未通过：" + result.stderr.decode("utf-8", errors="replace"))
    return hashlib.sha256(leafs[0]).hexdigest()


@dataclass(frozen=True)
class Enrollment:
    digest: str
    nonce: bytes
    ak_area: bytes
    ak_qualified: bytes
    ek_area: bytes
    certificates: tuple
    device_public: bytes
    trial_nv_public: bytes = b""
    trial_initial_counter: int = 0


def parse_request(raw):
    if not 8 < len(raw) <= 65536:
        raise ValueError("Invalid enrollment size")
    r = Reader(raw)
    magic = r.take(8)
    if magic not in (b"TDREQ002", b"TDREQ003") or field(r, 128) != authority.PRODUCT.encode():
        raise ValueError("Unknown enrollment format or product")
    nonce = field(r, 32)
    if len(nonce) != 32:
        raise ValueError("Invalid enrollment nonce")
    ak, qualified, ek = field(r, 256), field(r, 68), field(r, 1024)
    count = r.u32()
    if not 1 <= count <= 8:
        raise ValueError("Missing or excessive EK certificates")
    certificates = tuple(field(r, 8192) for _ in range(count))
    attest, signature = field(r, 1024), field(r, 72)
    verify_quote(ak, qualified, nonce, attest, signature)
    rsa_ek(ek)
    nv, initial = b"", 0
    if magic == b"TDREQ003":
        nv, initial = field(r, 128), int.from_bytes(r.take(8), "big")
        nv_attest, nv_signature = field(r, 1024), field(r, 72)
        verify_trial_evidence(ak, qualified, nonce, nv, initial, nv_attest, nv_signature)
    r.end()
    return Enrollment(hashlib.sha256(raw).hexdigest(), nonce, ak, qualified, ek, certificates, ak_public(ak), nv, initial)


def verify_trial_evidence(ak, qualified, nonce, nv, initial, attest, signature):
    if len(nv) != 14 or not 0 < initial <= 0xffffffffffffffff - 720:
        raise ValueError("Invalid trial counter")
    n = Reader(nv)
    if not 0x01500000 <= n.u32() <= 0x0150ffff or n.u16() != 11 or n.u32() != 0x22040014 or n.sized(32) or n.u16() != 8:
        raise ValueError("Unexpected trial NV policy")
    n.end()
    sig = Reader(signature)
    if (sig.u16(), sig.u16()) != (0x18, 11):
        raise ValueError("Invalid NV signature scheme")
    a, b = sig.sized(32), sig.sized(32); sig.end()
    if not a or not b:
        raise ValueError("Invalid NV signature")
    authority.verify_signature(ak_public(ak), attest + a.rjust(32, b"\0") + b.rjust(32, b"\0"))
    r = Reader(attest)
    if (r.u32(), r.u16()) != (0xff544347, 0x8014) or r.sized(68) != qualified or r.sized(64) != nonce:
        raise ValueError("NV attestation binding mismatch")
    r.take(16)
    if r.take(1) != b"\1":
        raise ValueError("Unsafe TPM clock")
    r.take(8)
    if r.sized(68) != sha256_name(nv) or r.u16() != 0 or r.sized(8) != initial.to_bytes(8, "big"):
        raise ValueError("NV contents do not match request")
    r.end()
