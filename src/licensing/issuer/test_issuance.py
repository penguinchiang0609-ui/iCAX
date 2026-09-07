import base64
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

import authority
from issuance import Issuer, canonical, read_request


class IssuanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="td-issuance-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.password = b"test-only-long-password"
        self.issuer = Issuer(self.root / "authority")
        self.issuer.initialize_test(self.password)
        device = ec.generate_private_key(ec.SECP256R1())
        body = dict(format="TDREQ-TEST-1", product=authority.PRODUCT, request_id="request-1",
                    device_public_key=base64.b64encode(authority.public_blob(device.public_key())).decode(),
                    nonce=base64.b64encode(b"n" * 32).decode())
        der = device.sign(b"TubeDesigner/test-request/v1\x00" + canonical(body), ec.ECDSA(hashes.SHA256()))
        r, s = utils.decode_dss_signature(der)
        self.envelope = dict(body=body, signature=base64.b64encode(r.to_bytes(32, "big") + s.to_bytes(32, "big")).decode())
        self.request = self.root / "request.tdreq"
        self.request.write_bytes(canonical(self.envelope))
        self.options = dict(customer="测试客户", kind=1, features=15, min_major=1, max_major=2)

    def issue(self, **changes):
        return self.issuer.issue(self.request, self.password, **(self.options | changes))

    def test_issue_export_and_reexport(self):
        identifier = self.issue()
        first, second = self.root / "first.tdlic", self.root / "second.tdlic"
        self.issuer.export(identifier, first)
        self.issuer.export(identifier, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        authority.verify_signature((self.issuer.directory / "test-public.blob").read_bytes(), first.read_bytes())
        with self.assertRaises(FileExistsError):
            self.issuer.export(identifier, first)
        self.assertEqual(len(self.issuer.records()), 1)

    def test_duplicate_request_rejected(self):
        self.issue()
        with self.assertRaises(sqlite3.IntegrityError):
            self.issue()
        self.assertEqual(len(self.issuer.records()), 1)

    def test_invalid_policy_and_password_leave_no_record(self):
        for changes in [dict(features=0), dict(min_major=9), dict(customer=""), dict(days=0)]:
            with self.assertRaises(ValueError):
                self.issue(**changes)
        with self.assertRaises(ValueError):
            self.issuer.issue(self.request, b"wrong", **self.options)
        self.assertEqual(self.issuer.records(), [])

    def test_trial(self):
        self.issue(kind=2, days=30)
        with closing(sqlite3.connect(self.issuer.directory / "issuance.sqlite")) as db:
            policy = json.loads(db.execute("SELECT policy FROM licenses").fetchone()[0])
        self.assertEqual(policy["expires_at"] - policy["not_before"], 30 * 86400)

    def test_untrusted_request_rejected(self):
        for modification in [dict(format="TDREQ-1"), dict(product="other"), dict(request_id="changed")]:
            changed = dict(self.envelope, body=self.envelope["body"] | modification)
            self.request.write_bytes(canonical(changed))
            with self.assertRaises(Exception):
                read_request(self.request)

    def test_duplicate_json_and_oversize_rejected(self):
        for raw in [b'{"body":{},"body":{},"signature":""}', b" " * 16385]:
            self.request.write_bytes(raw)
            with self.assertRaises(ValueError):
                read_request(self.request)

    def test_production_directory_rejected(self):
        with self.assertRaises(ValueError):
            Issuer(self.root).records()


if __name__ == "__main__":
    unittest.main()
