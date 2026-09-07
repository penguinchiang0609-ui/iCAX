"""Offline issuance workflow. v1 requests prove possession, NOT TPM provenance.

Until trusted enrollment is implemented, this workflow only accepts its own
explicitly test-marked authority directory. Never import a production key here.
"""
from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import sqlite3
import time
import uuid

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

import authority


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON field")
        result[key] = value
    return result


def read_request(path: Path):
    with path.open("rb") as stream:
        raw = stream.read(16385)
    if len(raw) > 16384:
        raise ValueError("Request exceeds 16 KiB")
    request = json.loads(raw, object_pairs_hook=unique_object)
    if not isinstance(request, dict) or set(request) != {"body", "signature"}:
        raise ValueError("Invalid request envelope")
    body = request["body"]
    if not isinstance(body, dict) or set(body) != {
        "format", "product", "request_id", "device_public_key", "nonce"
    }:
        raise ValueError("Invalid request fields")
    if any(not isinstance(v, str) for v in body.values()):
        raise ValueError("Request fields must be strings")
    if body["format"] != "TDREQ-TEST-1" or body["product"] != authority.PRODUCT:
        raise ValueError("Only test enrollment requests are supported")
    authority.identifier(body["request_id"])
    pub = base64.b64decode(body["device_public_key"], validate=True)
    nonce = base64.b64decode(body["nonce"], validate=True)
    signature = base64.b64decode(request["signature"], validate=True)
    if len(nonce) != 32 or len(signature) != 64:
        raise ValueError("Invalid nonce or signature length")
    r, s = int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big")
    authority.import_public_blob(pub).verify(utils.encode_dss_signature(r, s),
        b"TubeDesigner/test-request/v1\x00" + canonical(body), ec.ECDSA(hashes.SHA256()))
    return body, pub, hashlib.sha256(canonical(request)).hexdigest()


class Issuer:
    def __init__(self, directory: Path):
        self.directory = directory

    def initialize_test(self, password: bytes):
        # A new directory prevents mixing test credentials with production keys.
        self.directory.mkdir(parents=False, exist_ok=False)
        authority.initialize_key(self.directory / "test-private.pem", password)
        key = authority.load_private_key(self.directory / "test-private.pem", password)
        authority.exclusive_write(self.directory / "test-public.blob", authority.public_blob(key.public_key()))
        authority.exclusive_write(self.directory / "TEST-ONLY", b"Not a production licensing authority\n")

    def connect(self):
        if not (self.directory / "TEST-ONLY").is_file():
            raise ValueError("Not an initialized test authority")
        db = sqlite3.connect(self.directory / "issuance.sqlite", timeout=10)
        db.execute("PRAGMA synchronous=FULL")
        db.execute("""CREATE TABLE IF NOT EXISTS licenses (
            license_id TEXT PRIMARY KEY, request_id TEXT UNIQUE NOT NULL,
            request_hash TEXT NOT NULL, device_hash TEXT NOT NULL,
            customer TEXT NOT NULL, issued_at INTEGER NOT NULL,
            policy TEXT NOT NULL, certificate BLOB NOT NULL)""")
        return db

    def issue(self, request_path: Path, password: bytes, *, customer: str,
              kind: int, features: int, min_major: int, max_major: int, days: int = 30):
        body, pub, request_hash = read_request(request_path)
        if not isinstance(customer, str) or not customer.strip() or len(customer) > 256:
            raise ValueError("Customer name is required (maximum 256 characters)")
        if type(days) is not int or not 1 <= days <= 365:
            raise ValueError("Trial duration must be 1..365 days")
        db = self.connect()
        try:
            db.execute("BEGIN IMMEDIATE")
            key = authority.load_private_key(self.directory / "test-private.pem", password)
            if authority.public_blob(key.public_key()) != (self.directory / "test-public.blob").read_bytes():
                raise ValueError("Authority public/private key mismatch")
            issued_at = int(time.time())
            license_id = "test-" + str(uuid.uuid4())
            policy = dict(kind=kind, features=features, min_major=min_major, max_major=max_major,
                          issued_at=issued_at, not_before=issued_at if kind == 2 else 0,
                          expires_at=issued_at + days * 86400 if kind == 2 else 0)
            certificate = authority.sign_body(key, authority.encode_body(
                issuer_id="test-only", license_id=license_id, request_id=body["request_id"],
                customer_id=str(uuid.uuid4()), device_public_key=pub, **policy))
            authority.verify_signature(authority.public_blob(key.public_key()), certificate)
            db.execute("INSERT INTO licenses VALUES (?,?,?,?,?,?,?,?)", (
                license_id, body["request_id"], request_hash, hashlib.sha256(pub).hexdigest(),
                customer.strip(), issued_at, canonical(policy).decode(), certificate))
            db.commit()
            return license_id
        finally:
            db.close()

    def records(self):
        db = self.connect()
        try:
            return db.execute("SELECT license_id, customer, request_id, device_hash, issued_at "
                              "FROM licenses ORDER BY issued_at DESC, license_id").fetchall()
        finally:
            db.close()

    def export(self, license_id: str, destination: Path):
        db = self.connect()
        try:
            row = db.execute("SELECT certificate FROM licenses WHERE license_id=?", (license_id,)).fetchone()
            if row is None:
                raise ValueError("Unknown license")
            # The exact committed certificate can be exported again after a crash.
            authority.exclusive_write(destination, row[0])
        finally:
            db.close()
