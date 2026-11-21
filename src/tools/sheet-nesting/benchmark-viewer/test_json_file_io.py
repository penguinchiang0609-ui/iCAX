"""Completed JSON reads preserve producer paths, bytes and cache identities."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

import json_file_io
from construction_store import read_json
from materials_store import RecordFiles
from server import JsonFiles, ROOT


class JsonSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.base = ROOT / "output/work/sheet-nesting/benchmark-viewer-tests"
        self.base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=self.base)
        self.root = Path(self.temporary.name).resolve()
        self.target = self.root / "running.trace-index.json"

    def tearDown(self):
        assert self.root.is_relative_to(self.base.resolve()) and self.root != self.base.resolve()
        self.temporary.cleanup()

    def test_completed_record_bytes_and_original_path_are_preserved(self):
        self.target.write_bytes(b'{"version":"before"}')
        replacement = self.root / "next.json"
        replacement.write_bytes(b'{"version":"after"}')
        with json_file_io.open_json_snapshot(self.target) as reader:
            self.assertEqual(reader.read(), b'{"version":"before"}')
            self.assertEqual([p.name for p in self.root.glob("*.trace-index.json")], [self.target.name])
        os.replace(replacement, self.target)
        self.assertEqual(self.target.read_bytes(), b'{"version":"after"}')
        self.assertFalse(list(self.root.glob(".index-reader-*.snapshot")))

    def test_all_index_readers_keep_producer_cache_keys_and_original_byte_hashes(self):
        old, new = b'{"version":"before"}', b'{"version":"after"}'
        for kind in ("construction", "server", "materials"):
            self.target.write_bytes(old)
            replacement = self.root / "next.json"
            replacement.write_bytes(new)

            with self.subTest(kind=kind):
                if kind == "construction":
                    value = read_json(self.target)
                else:
                    cache = JsonFiles() if kind == "server" else RecordFiles()
                    value = cache.read(self.target)
                    self.assertEqual(set(cache.values), {self.target})
                    if kind == "server":
                        self.assertEqual(cache.digest(self.target), hashlib.sha256(old).hexdigest())
            self.assertEqual(value, {"version": "before"})
            self.assertEqual(self.target.read_bytes(), old)
            os.replace(replacement, self.target)
            self.assertEqual(read_json(self.target), {"version": "after"})
            self.assertFalse(list(self.root.glob(".index-reader-*.snapshot")))

    def test_reader_exception_closes_handle_and_preserves_producer(self):
        self.target.write_bytes(b'{"keep":true}')
        with self.assertRaisesRegex(RuntimeError, "reader failed"):
            with json_file_io.open_json_snapshot(self.target):
                raise RuntimeError("reader failed")
        self.assertEqual(self.target.read_bytes(), b'{"keep":true}')
        self.assertFalse(list(self.root.glob(".index-reader-*.snapshot")))

if __name__ == "__main__":
    unittest.main()
