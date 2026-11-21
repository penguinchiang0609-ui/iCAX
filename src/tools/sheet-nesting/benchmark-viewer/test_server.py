"""Live-report contract tests; no nesting solver is run."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from server import BenchmarkStore, ROOT


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")
    return hashlib.sha256(path.read_bytes()).hexdigest()


class LiveReportTests(unittest.TestCase):
    def setUp(self):
        self.test_base = (ROOT / "output/work/sheet-nesting/benchmark-viewer-tests").resolve()
        self.test_base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=self.test_base)
        self.root = Path(self.temporary.name).resolve()
        assert self.root.is_relative_to(self.test_base) and self.root != self.test_base
        self.batch = self.root / "batch"
        self.input = self.root / "original.problem"
        self.input.write_text("original immutable input", encoding="utf-8")
        self.input_hash = hashlib.sha256(self.input.read_bytes()).hexdigest()
        self.originals = [dict(id="first", source="sample", status="ready", input=str(self.input),
                               inputSha256=self.input_hash, target=dict(kind="boards", boards=1),
                               qualification=dict(targetComparable=True)),
                          dict(id="second", source="sample", status="ready", input=str(self.input),
                               inputSha256=self.input_hash, target={}, qualification={}),
                          dict(id="unavailable", source="sample", status="blocked", input=None,
                               target={}, qualification={}, blockedReasons=["original condition missing"])]
        write(self.batch / "indexes/prepared-index-1420.json", dict(rows=self.originals))
        write(self.batch / "case-scores.json", dict(cases=[dict(id=r["id"]) for r in self.originals]))
        write(self.batch / "run-configuration.json", dict(strategy="plane.initial"))
        self.problem = dict(parts=[dict(id="p", quantity=1, geometry=dict(radius=0, outer=[[0, 0], [2, 0], [2, 2], [0, 2]], holes=[]))],
                            stocks=[dict(id="s", width=10, height=10)])
        self.result = dict(valid=True, complete=True, placed=1, layouts=[dict(stock="s", instance=0, width=10, height=10,
                            placements=[dict(part="p", instance=0, x=0, y=0, angle=0)])])

    def tearDown(self):
        assert self.root.resolve().is_relative_to(self.test_base) and self.root != self.test_base
        self.temporary.cleanup()

    def native(self, index=1, public=False):
        record = dict(problem=self.problem, **({"runs": [{"construction": self.result, "selected": {"invalid": "must not be used"}}]} if public else {"result": self.result}))
        raw = self.batch / f"raw/{index:04d}.json"
        self.raw_hash = write(raw, record)
        self.execution = dict(id=self.originals[index-1]["id"], status="native-result", inputSha256=self.input_hash,
                              raw=str(raw), rawSha256=self.raw_hash, nativeValid=True, nativeComplete=True,
                              nativePlaced=1, nativeBoards=1, required=1, nativeWallSeconds=1, wholeProcessWallSeconds=1.1)
        write(self.batch / f"executions/{index:04d}.json", self.execution)

    def test_completed_case_appears_before_scorer_without_claiming_verified(self):
        store = BenchmarkStore(self.batch, self.root)
        before = store.cases()
        self.assertEqual([r["index"] for r in before["rows"]], [1, 2, 3])
        self.native()
        store.refresh(force=True)
        row = store.cases()["rows"][0]
        self.assertGreater(store.revision, before["revision"])
        self.assertEqual(row["executionStatus"], "native-result")
        self.assertIsNone(row["geometryValid"])
        self.assertIsNone(row["qualityReached"])
        self.assertFalse(row["certifiedComplete"])
        self.assertEqual(store.detail(1)["result"], self.result)

    def test_review_and_scoring_arrive_separately_and_stale_score_is_not_reused(self):
        self.native()
        review = dict(id="first", inputSha256=self.input_hash, rawSha256=self.raw_hash,
                      valid=True, complete=True, placed=1, boards=1, errors=[])
        digest = write(self.batch / "reviews/0001.json", review)
        store = BenchmarkStore(self.batch, self.root)
        self.assertTrue(store.cases()["rows"][0]["certifiedComplete"])
        self.assertIsNone(store.cases()["rows"][0]["qualityReached"])
        write(self.batch / "case-scores.json", dict(cases=[dict(id="first", rawSha256=self.raw_hash,
              reviewSha256=digest, qualityReached=True)]))
        store.refresh(force=True)
        self.assertTrue(store.cases()["rows"][0]["qualityReached"])
        review.update(valid=False, complete=False, errors=["independent geometry conflict"])
        write(self.batch / "reviews/0001.json", review)
        store.refresh(force=True)
        self.assertFalse(store.cases()["rows"][0]["certifiedComplete"])
        self.assertIsNone(store.cases()["rows"][0]["qualityReached"])

    def test_public_bench_uses_original_construction_and_does_not_change_files(self):
        self.native(public=True)
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.batch.rglob("*.json")}
        store = BenchmarkStore(self.batch, self.root)
        self.assertEqual(store.detail(1)["result"], self.result)
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in before})
        self.assertEqual(hashlib.sha256(self.input.read_bytes()).hexdigest(), self.input_hash)

    def test_timeout_blocked_and_incomplete_file_updates_do_not_break_live_view(self):
        self.native()
        self.execution.update(status="process-timeout", nativeWallSeconds=None, wholeProcessWallSeconds=65,
                              nativeValid=None, nativeComplete=None)
        write(self.batch / "executions/0001.json", self.execution)
        store = BenchmarkStore(self.batch, self.root)
        self.assertFalse(store.cases()["rows"][0]["nativeWithinMinute"])
        self.assertEqual(store.detail(3)["evidence"], {})
        (self.batch / "case-scores.json").write_text("{partial update", encoding="utf-8")
        store.refresh(force=True)
        self.assertEqual(store.summary()["live"]["executed"], 1)


if __name__ == "__main__":
    unittest.main()
