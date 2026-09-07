"""Offline authority: validated EK chain + TPM-locked activation package."""
import hashlib
import json
from pathlib import Path
import secrets
import shutil
import sqlite3
import struct
import time
import uuid

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

import authority
from enrollment import parse_request, verify_ek_native
from tpm_credential import make_credential, sha256_name


def field(value):
    return struct.pack(">I", len(value)) + value


def read_bounded(path, limit):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("File exceeds size limit")
    return data


def activation_package(key, request, certificate):
    credential = secrets.token_bytes(32)
    blob, secret = make_credential(request.ek_area, sha256_name(request.ak_area), credential)
    nonce = secrets.token_bytes(12)
    digest = bytes.fromhex(request.digest)
    encrypted = AESGCM(credential).encrypt(nonce, certificate, digest)
    body = b"TDACT001" + b"".join(field(v) for v in (digest, sha256_name(request.ak_area), blob, secret, nonce, encrypted))
    r, s = utils.decode_dss_signature(key.sign(body, ec.ECDSA(hashes.SHA256())))
    return body + r.to_bytes(32, "big") + s.to_bytes(32, "big")


class ProductionIssuer:
    def __init__(self, directory):
        self.directory = Path(directory)

    def initialize(self, password):
        if len(password) < 16:
            raise ValueError("私钥口令至少 16 个 UTF-8 字节")
        self.directory.mkdir(exist_ok=False)
        authority.initialize_key(self.directory / "issuer-private.pem", password)
        key = authority.load_private_key(self.directory / "issuer-private.pem", password)
        authority.exclusive_write(self.directory / "issuer-public.blob", authority.public_blob(key.public_key()))
        for name in ("manufacturer-roots", "intermediates"):
            (self.directory / name).mkdir()
        authority.exclusive_write(self.directory / "issuer.json", json.dumps({
            "format": 1, "issuer_id": "td-" + str(uuid.uuid4()), "public_sha256": hashlib.sha256(
                authority.public_blob(key.public_key())).hexdigest()}, sort_keys=True).encode())

    def configuration(self):
        config = json.loads(read_bounded(self.directory / "issuer.json", 4096))
        if set(config) != {"format", "issuer_id", "public_sha256"} or config["format"] != 1:
            raise ValueError("Invalid issuer configuration")
        authority.identifier(config["issuer_id"])
        public = read_bounded(self.directory / "issuer-public.blob", 72)
        authority.import_public_blob(public)
        if hashlib.sha256(public).hexdigest() != config["public_sha256"]:
            raise ValueError("Issuer public key changed")
        return config, public

    def connect(self):
        self.configuration()
        db = sqlite3.connect(self.directory / "issuance.sqlite", timeout=10)
        db.execute("PRAGMA synchronous=FULL")
        db.execute("""CREATE TABLE IF NOT EXISTS issued (
            id TEXT PRIMARY KEY, request_hash TEXT UNIQUE NOT NULL, device_hash TEXT NOT NULL,
            customer TEXT NOT NULL, issued_at INTEGER NOT NULL, policy TEXT NOT NULL,
            certificate_hash TEXT NOT NULL, package BLOB NOT NULL, ek_certificate_hash TEXT NOT NULL)""")
        db.execute("CREATE TABLE IF NOT EXISTS trial_devices (device_hash TEXT PRIMARY KEY, license_id TEXT NOT NULL)")
        return db

    def inspect(self, path):
        request = parse_request(read_bounded(path, 65536))
        def certificates(folder):
            files = list((self.directory / folder).glob("*.cer"))
            if len(files) > 256:
                raise ValueError("Too many trust certificates")
            return [read_bounded(p, 16384) for p in files]
        fingerprint = verify_ek_native(request.ek_area, request.certificates,
                                certificates("manufacturer-roots"), certificates("intermediates"),
                                native_verifier=self.native_verifier())
        return request, fingerprint

    @staticmethod
    def native_verifier():
        # Bundled beside issuer code, never resolved through PATH/customer input.
        tool = Path(__file__).resolve().parent / "td-license-verify-ek.exe"
        if not tool.is_file():
            raise ValueError("缺少随签发工具分发的 td-license-verify-ek.exe，请先构建签发工具包")
        return tool

    def issue(self, request_path, password, *, customer, kind, features, min_major, max_major, days=30):
        if type(kind) is not int or kind not in (1, 2):
            raise ValueError("Unknown license kind")
        if not isinstance(customer, str) or not customer.strip() or len(customer) > 256:
            raise ValueError("请填写客户名称（最多 256 字符）")
        request, fingerprint = self.inspect(request_path)
        config, public = self.configuration()
        key = authority.load_private_key(self.directory / "issuer-private.pem", password)
        if authority.public_blob(key.public_key()) != public:
            raise ValueError("Issuer key mismatch")
        issued_at, identifier = int(time.time()), str(uuid.uuid4())
        policy = dict(kind=kind, features=features, min_major=min_major, max_major=max_major, issued_at=issued_at)
        if kind == 2:
            if not request.trial_nv_public or type(days) is not int or not 1 <= days <= 30:
                raise ValueError("试用必须有 NV 绑定的试用申请，天数限定 1 至 30 天")
            policy.update(not_before=issued_at, expires_at=issued_at + days * 86400)
        binding = dict(trial_nv_public=request.trial_nv_public, trial_initial_counter=request.trial_initial_counter,
                       trial_quantum_seconds=3600) if kind == 2 else {}
        certificate = authority.sign_body(key, authority.encode_body(issuer_id=config["issuer_id"],
            license_id=identifier, request_id=request.digest, customer_id=str(uuid.uuid4()),
            device_public_key=request.device_public, **policy, **binding))
        package = activation_package(key, request, certificate)
        db = self.connect()
        try:
            db.execute("BEGIN IMMEDIATE")
            if kind == 2:
                db.execute("INSERT INTO trial_devices VALUES (?,?)", (hashlib.sha256(request.ek_area).hexdigest(), identifier))
            db.execute("INSERT INTO issued VALUES (?,?,?,?,?,?,?,?,?)", (
                identifier, request.digest, hashlib.sha256(request.ek_area).hexdigest(), customer.strip(), issued_at,
                json.dumps(policy, sort_keys=True), hashlib.sha256(certificate).hexdigest(), package, fingerprint))
            db.commit()
        finally:
            db.close()
        return identifier

    def records(self):
        db = self.connect()
        try:
            return db.execute("SELECT id, customer, request_hash, device_hash, issued_at FROM issued ORDER BY issued_at DESC, id").fetchall()
        finally:
            db.close()

    def export(self, identifier, output):
        db = self.connect()
        try:
            row = db.execute("SELECT package FROM issued WHERE id=?", (identifier,)).fetchone()
            if row is None:
                raise ValueError("Unknown issuance record")
            _, public = self.configuration()
            authority.verify_signature(public, row[0])
            authority.exclusive_write(Path(output), row[0])
        finally:
            db.close()

    def export_build_trust(self, output):
        config, public = self.configuration()
        text = "#pragma once\n#include <array>\n#include <string_view>\nnamespace tube::license::trust {\n"
        text += 'inline constexpr std::string_view Issuer = ' + json.dumps(config["issuer_id"]) + ';\n'
        text += "inline constexpr std::array<unsigned char, 72> PublicKey{" + ",".join(f"0x{b:02x}" for b in public) + "};\n}\n"
        authority.exclusive_write(Path(output), text.encode("ascii"))

    def backup(self, destination):
        """Consistent ledger snapshot; encrypted key is copied without decrypting."""
        destination = Path(destination).resolve()
        if destination == self.directory.resolve() or self.directory.resolve() in destination.parents:
            raise ValueError("备份目录不能放在签发库内部")
        self.configuration()
        destination.mkdir(exist_ok=False)
        # Keep incomplete backups clearly marked if a disk error interrupts copying.
        marker = destination / "BACKUP_INCOMPLETE"
        authority.exclusive_write(marker, b"Do not restore an incomplete backup.")
        source = self.connect()
        target = sqlite3.connect(destination / "issuance.sqlite")
        try:
            source.backup(target)
        finally:
            target.close()
            source.close()
        for name in ("issuer-private.pem", "issuer-public.blob", "issuer.json"):
            shutil.copyfile(self.directory / name, destination / name)
        for name in ("manufacturer-roots", "intermediates"):
            shutil.copytree(self.directory / name, destination / name)
        marker.unlink()

    def add_certificate(self, source, *, trusted_root=False):
        # The UI must require independent verification before trusting a root.
        der = read_bounded(source, 16384)
        import subprocess
        import tempfile
        with tempfile.TemporaryDirectory(prefix="td-ca-check-") as directory:
            path = Path(directory) / "ca.cer"
            path.write_bytes(der)
            result = subprocess.run([str(self.native_verifier()), "--check-ca", str(path), "root" if trusted_root else "intermediate"],
                capture_output=True, timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            if result.returncode != 0:
                raise ValueError(result.stderr.decode("utf-8", errors="replace"))
        folder = "manufacturer-roots" if trusted_root else "intermediates"
        authority.exclusive_write(self.directory / folder / (hashlib.sha256(der).hexdigest() + ".cer"), der)
