"""Read-only materials, result binding and download boundary contracts."""
import hashlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from materials_store import MaterialsStore, RecordFiles, calculate_metrics
from server import ROOT, ViewerHandler


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")


class MaterialsReaderTests(unittest.TestCase):
    def setUp(self):
        self.test_base = (ROOT / "output/work/sheet-nesting/benchmark-viewer-tests").resolve()
        self.test_base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=self.test_base)
        self.root = Path(self.temporary.name).resolve()
        self.plane = self.root / "TestData/Plane"
        self.catalog = self.plane / "catalog"
        self.dxf = self.plane / "DXF/original.dxf"
        self.dxf.parent.mkdir(parents=True)
        self.dxf.write_bytes(b"original DXF bytes; never parsed or altered by viewer\r\n")
        self.hash = hashlib.sha256(self.dxf.read_bytes()).hexdigest()
        asset = dict(id="asset-1", name="original.dxf", relativePath="DXF/original.dxf", sha256=self.hash,
                     sourcePaths=["package-a/parts/original.dxf"], units=dict(code=0, label="default mm", scaleToMm=1, assumed=True),
                     geometry=dict(status="ready", partCount=1), collections=["metal"])
        orphan = dict(asset, id="asset-2", sourcePaths=["package-b/unused.dxf"], collections=[],
                      units=dict(code=4, label="mm", scaleToMm=1, assumed=False))
        first = dict(id="task-1", label="First task", collection="metal", kind="benchmark", status="ready", inputSha256="current-input",
                     parts=[dict(assetId="asset-1", quantity=5, quantitySource="source-task", sourcePath="package-a/parts/original.dxf")],
                     stocks=[dict(widthMm=3000, heightMm=6000, unlimited=True, quantity=None)], missingAssets=[],
                     rules=dict(edgeMarginMm=.5, partGapMm=.2, freeRotation=True, allowCommonEdges=True), benchmark=dict(status="pending"))
        missing = dict(first, id="task-2", label="Missing source", status="missing-assets", inputSha256=None,
                       parts=[dict(assetId=None, quantity=1, quantitySource="benchmark-default-one")], missingAssets=["unavailable.dxf"],
                       benchmark=dict(status="blocked"))
        reference = dict(first, id="task-3", label="Source nest", kind="reference", status="reference", benchmark=dict(status="blocked"))
        self.document = dict(schemaVersion=1, generatedAt="2026-11-08T04:00:00+08:00", defaults=first["rules"],
                             archives=[dict(name="package-a.zip", directory="package-a"), dict(name="package-b.zip", directory="package-b")],
                             assets=[asset, orphan], tasks=[first, missing, reference])
        write(self.catalog / "catalog.json", self.document)
        for filename in ("dxf_inventory.csv", "task_parts.csv", "tasks.csv", "pk_results.csv"):
            (self.catalog / filename).write_text("original,csv\n1,2\n", encoding="utf-8-sig")
        self.store = MaterialsStore(self.root)

    def tearDown(self):
        assert self.root.is_relative_to(self.test_base) and self.root != self.test_base
        self.temporary.cleanup()

    def result(self, **overrides):
        record = dict(taskId="task-1", runSignature=dict(inputSha256="current-input"),
                      benchmark=dict(status="finished", valid=True, complete=True, placed=5, required=5,
                                     boards=1, utilization=.2, nativeWallSeconds=2.3, layouts=[]))
        record.update(overrides)
        write(self.catalog / "results/task-1.json", record)
        self.store.refresh(force=True)

    def live(self, *, descriptor=None, payload=None):
        self.document["executionProfile"] = dict(executableSha256="current-algorithm")
        write(self.catalog / "catalog.json", self.document)
        record = dict(taskId="task-1", active=True, startedAtUnix=995,
                      runSignature=dict(inputSha256="current-input", executableSha256="current-algorithm"),
                      incumbentPath="catalog/raw/current.incumbent.json")
        if descriptor:
            record.update(descriptor)
        candidate = dict(live=True, firstCompleteWallSeconds=.5, wholeJobWallSeconds=1.5,
                         problem=dict(parts=[dict(id="p", quantity=5, geometry=dict(radius=0, outer=[[0, 0], [1, 0], [0, 1]], holes=[]))]),
                         result=dict(valid=True, complete=True, boards=1, placed=5,
                                     layouts=[dict(stock="s", instance=0, width=10, height=10,
                                                   placements=[dict(part="p", instance=i, x=2*i, y=0, angle=0) for i in range(5)])]),
                         validation=dict(materialArea=2.5), settings={})
        if payload:
            candidate.update(payload)
        write(self.catalog / "raw/current.incumbent.json", candidate)
        write(self.catalog / "live/task-1.json", record)
        self.store.refresh(force=True)

    def test_live_complete_incumbent_is_immediate_but_not_a_final_result(self):
        with patch("materials_store.time.time", return_value=1000):
            self.live()
        benchmark = self.store.task("task-1")["task"]["benchmark"]
        self.assertEqual(benchmark["status"], "constructing")
        self.assertEqual(benchmark["nativeWallSeconds"], 5)
        self.assertEqual(benchmark["snapshotWallSeconds"], 1.5)
        self.assertEqual(benchmark["firstCompleteWallSeconds"], .5)
        self.assertEqual(benchmark["timingScope"], "live-current")
        self.assertEqual(benchmark["utilization"], .025)
        summary = self.store.summary()["summary"]
        self.assertEqual((summary["executed"], summary["complete"], summary["constructing"]), (0, 0, 1))
        self.assertEqual(self.store.tasks(filter="executed")["rows"][0]["benchmark"]["status"], "constructing")
        revision = self.store.revision
        with patch("materials_store.time.time", return_value=1002):
            self.store.refresh(force=True)
        self.assertGreater(self.store.revision, revision)
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["nativeWallSeconds"], 7)

    def test_live_requires_matching_input_algorithm_and_whitelisted_candidate(self):
        for changes in (dict(taskId="different"), dict(runSignature=dict(inputSha256="old-input", executableSha256="current-algorithm")),
                        dict(runSignature=dict(inputSha256="current-input", executableSha256="old-algorithm")),
                        dict(incumbentPath="catalog/catalog.json"), dict(incumbentPath="catalog/raw/../outside.incumbent.json"),
                        dict(incumbentPath="DXF/original.dxf")):
            with self.subTest(changes=changes):
                self.live(descriptor=changes)
                task = self.store.task("task-1")["task"]
                self.assertNotEqual(task["benchmark"]["status"], "constructing")
                self.assertIn("liveIssue", task)
                self.assertEqual(self.store.summary()["summary"]["executed"], 0)
        self.live(payload=dict(result=dict(valid=False, complete=True, placed=5, layouts=[])))
        self.assertNotEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "constructing")
        self.live(payload=dict(result=dict(valid=True, complete=False, placed=4, layouts=[])))
        self.assertNotEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "constructing")

    def test_live_and_final_remnants_keep_original_native_values(self):
        remnants = dict(measured=True, lastSheetRegularArea=24, lastSheetAreaUpperBound=30,
                        finalLayoutIndices=[0], perLayout=[dict(verified=True, area=24, areaUpperBound=30,
                            resolution=64, bounds=dict(x=1, y=2, width=4, height=6))])
        self.live()
        path = self.catalog / "raw/current.incumbent.json"
        payload = json.loads(path.read_text(encoding="utf-8"))
        payload["result"]["remnants"] = remnants
        write(path, payload)
        self.store.refresh(force=True)
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["remnants"], remnants)
        self.result(runSignature=dict(inputSha256="current-input", executableSha256="current-algorithm"),
                    benchmark=dict(status="finished", valid=True, complete=True, placed=5, required=5,
                                   boards=1, nativeWallSeconds=2.3, endToEndWallSeconds=2.7, remnants=remnants))
        benchmark = self.store.task("task-1")["task"]["benchmark"]
        self.assertEqual(benchmark["remnants"], remnants)
        self.assertEqual(benchmark["endToEndWallSeconds"], 2.7)

    def test_final_result_wins_over_active_live_and_inactive_live_is_removed(self):
        self.live()
        self.live(descriptor=dict(active=False))
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "pending")
        self.live()
        self.result(runSignature=dict(inputSha256="current-input", executableSha256="current-algorithm"))
        task = self.store.task("task-1")["task"]
        self.assertEqual(task["benchmark"]["status"], "finished")
        self.assertEqual(task["benchmark"]["nativeWallSeconds"], 2.3)
        self.assertEqual(self.store.summary()["summary"]["constructing"], 0)
        self.assertEqual(self.store.summary()["summary"]["complete"], 1)

    def test_live_without_start_clock_reports_snapshot_time_only(self):
        self.live(descriptor=dict(startedAtUnix=None))
        benchmark = self.store.task("task-1")["task"]["benchmark"]
        self.assertEqual(benchmark["timingScope"], "live-snapshot")
        self.assertEqual(benchmark["nativeWallSeconds"], 1.5)

    def test_complete_final_report_metrics_are_used_without_geometry_recalculation(self):
        with patch("materials_store.calculate_metrics", side_effect=AssertionError("new metrics must be reused")):
            self.result(benchmark=dict(status="finished", valid=True, complete=True, placed=5, required=5,
                                       materialArea=2.5, sheetArea=100, rollUsage=None, materialAreaSource="native-validation"))
            benchmark = self.store.task("task-1")["task"]["benchmark"]
            self.assertEqual(benchmark["materialArea"], 2.5)
            self.assertEqual(benchmark["materialAreaSource"], "native-validation")

    def test_legacy_final_metrics_are_cached_until_result_file_changes(self):
        with patch("materials_store.calculate_metrics", wraps=calculate_metrics) as measure:
            self.result()
            self.assertEqual(measure.call_count, 1)
            write(self.catalog / "progress.json", dict(status="running", currentTasks=["task-1"]))
            self.store.refresh(force=True)
            self.assertEqual(measure.call_count, 1)
            self.result(benchmark=dict(status="finished", valid=True, complete=True, placed=5, required=5, nativeWallSeconds=3.003))
            self.assertEqual(measure.call_count, 2)

    def test_live_report_metrics_cache_tracks_incumbent_not_clock(self):
        self.document["tasks"][0]["sourceStock"] = dict(kind="roll", width=10, height=-1)
        with patch("materials_store.calculate_metrics", wraps=calculate_metrics) as measure:
            with patch("materials_store.time.time", return_value=1000):
                self.live()
            self.assertEqual(measure.call_count, 1)
            with patch("materials_store.time.time", return_value=1001):
                self.store.refresh(force=True)
            self.assertEqual(measure.call_count, 1)
            self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["nativeWallSeconds"], 6)
            path = self.catalog / "raw/current.incumbent.json"
            candidate = json.loads(path.read_text(encoding="utf-8"))
            candidate["result"]["layouts"][0]["placements"][0]["y"] = 100
            write(path, candidate)
            with patch("materials_store.time.time", return_value=1002):
                self.store.refresh(force=True)
            self.assertEqual(measure.call_count, 2)
            self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["rollUsage"]["totalLength"], 101.5)

    def test_catalogue_pagination_source_filters_and_default_provenance(self):
        summary = self.store.summary()
        self.assertEqual(summary["summary"]["executed"], 0)
        self.assertEqual(summary["summary"]["pending"], 1)
        self.assertEqual(summary["summary"]["assumedQuantityRows"], 1)
        self.assertEqual(summary["summary"]["missingAssets"], 1)
        self.assertEqual(summary["packages"], ["package-a", "package-b"])
        self.assertEqual(self.store.tasks(offset=1, limit=1)["rows"][0]["id"], "task-2")
        self.assertEqual(self.store.tasks(filter="ready")["total"], 1)
        self.assertEqual(self.store.tasks(filter="reference")["total"], 1)
        self.assertEqual(self.store.tasks(filter="missing")["total"], 1)
        self.assertEqual(self.store.tasks(filter="assumed-units")["total"], 2)
        self.assertEqual(self.store.tasks(q="first")["rows"][0]["required"], 5)
        self.assertEqual(self.store.assets(collection="package-b")["rows"][0]["id"], "asset-2")
        self.assertEqual(self.store.assets(filter="ready")["total"], 1)
        self.assertEqual(self.store.task("task-1")["task"]["stocks"][0]["quantity"], None)
        self.assertEqual(self.store.task("task-2")["task"]["parts"][0]["quantitySource"], "benchmark-default-one")

    def test_native_result_arrives_incrementally_without_altering_catalogue(self):
        before = (self.catalog / "catalog.json").read_bytes()
        revision = self.store.revision
        self.result()
        self.assertGreater(self.store.revision, revision)
        self.assertEqual(self.store.summary()["summary"]["executed"], 1)
        self.assertEqual(self.store.summary()["summary"]["complete"], 1)
        self.assertEqual(self.store.tasks(filter="executed")["rows"][0]["benchmark"]["nativeWallSeconds"], 2.3)
        self.assertEqual((self.catalog / "catalog.json").read_bytes(), before)
        self.assertEqual(self.dxf.read_bytes(), b"original DXF bytes; never parsed or altered by viewer\r\n")

    def test_import_blocked_filter_includes_unsupported_and_review_tasks(self):
        original = self.document["tasks"][0]
        for task_id, status, reason in (("unsupported-task", "unsupported-geometry", "Unsupported DXF entity ARC"),
                                        ("review-task", "needs-review", "Quantity mapping is ambiguous")):
            self.document["tasks"].append(dict(original, id=task_id, status=status, issues=[reason],
                                                parts=[dict(assetId="asset-2", quantity=1, quantitySource="source")],
                                                benchmark=dict(status="blocked")))
        write(self.catalog / "catalog.json", self.document)
        self.store.refresh(force=True)
        page = self.store.tasks(filter="import-blocked")
        self.assertEqual({row["id"] for row in page["rows"]}, {"unsupported-task", "review-task"})
        self.assertEqual(page["total"], 2)
        self.assertIn("ARC", self.store.task("unsupported-task")["task"]["issues"][0])
        self.assertEqual([row["id"] for row in self.store.assets(filter="import-blocked")["rows"]], ["asset-2"])

    def test_source_contours_are_runnable_without_pretending_dxf_exists(self):
        task = self.document["tasks"][1]
        task.update(status="ready", inputSource="source-contours", missingAssets=[], missingDxfAssets=["original-shape.dxf"],
                    parts=[dict(id="source-part", assetId=None, requestedDxf="original-shape.dxf", quantity=7, quantitySource="original-job",
                                geometrySource=dict(kind="source-contour", path="package-a/shapes_raw.json", sha256="source-sha",
                                                    shapeHash="shape-123", units=dict(code=0, scaleToMm=1, assumed=True)))],
                    benchmark=dict(status="pending"))
        write(self.catalog / "catalog.json", self.document)
        self.store.refresh(force=True)
        summary = self.store.summary()["summary"]
        self.assertEqual(summary["ready"], 2)
        self.assertEqual(summary["sourceContourTasks"], 1)
        self.assertEqual(summary["missingDxfTasks"], 1)
        self.assertEqual(summary["missingAssets"], 0)
        self.assertEqual(summary["assets"], 2)
        row = self.store.tasks(filter="source-contours")["rows"][0]
        self.assertEqual((row["id"], row["required"], row["assumedUnits"]), ("task-2", 7, True))
        self.assertEqual(row["missingDxfAssets"], ["original-shape.dxf"])
        self.assertEqual(self.store.tasks(filter="missing-dxf")["total"], 1)
        self.assertEqual(self.store.tasks(filter="missing")["total"], 0)
        self.assertIn("task-2", [row["id"] for row in self.store.tasks(collection="package-a")["rows"]])
        detail = self.store.task("task-2")
        self.assertEqual(detail["assets"], [])
        self.assertEqual(detail["task"]["parts"][0]["geometrySource"]["sha256"], "source-sha")
        with self.assertRaises(KeyError):
            self.store.dxf_file("source-part")

    def test_invalid_source_is_distinct_from_missing_dxf_and_keeps_evidence(self):
        task = self.document["tasks"][1]
        evidence = dict(shapeHash="bad-shape", verifiedReason="non-adjacent edges 0/2 intersect", intersection=[2, 3])
        task.update(status="source-invalid", inputSource="source-contours", invalidSourceShapes=[evidence],
                    missingDxfAssets=["invalid-original.dxf"])
        write(self.catalog / "catalog.json", self.document)
        self.store.refresh(force=True)
        summary = self.store.summary()["summary"]
        self.assertEqual(summary["sourceInvalidTasks"], 1)
        self.assertEqual(summary["sourceContourTasks"], 0)
        self.assertEqual(summary["missingAssets"], 0)
        self.assertEqual(self.store.tasks(filter="source-invalid")["rows"][0]["id"], "task-2")
        self.assertEqual(self.store.tasks(filter="missing")["total"], 0)
        self.assertEqual(self.store.task("task-2")["task"]["invalidSourceShapes"], [evidence])

    def test_wrong_task_and_stale_input_are_not_used_as_current_results(self):
        self.result(taskId="different-task")
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "binding-error")
        self.assertEqual(self.store.summary()["summary"]["executed"], 0)
        self.result(runSignature=dict(inputSha256="old-input"))
        benchmark = self.store.task("task-1")["task"]["benchmark"]
        self.assertEqual(benchmark["status"], "stale-input")
        self.assertNotIn("layouts", benchmark)
        self.assertEqual(self.store.tasks(filter="executed")["total"], 0)
        self.assertEqual(self.store.tasks(filter="pending")["total"], 1)

    def test_incomplete_writes_keep_last_complete_native_record(self):
        self.result()
        (self.catalog / "results/task-1.json").write_text('{"taskId":', encoding="utf-8")
        self.store.refresh(force=True)
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["placed"], 5)
        (self.catalog / "catalog.json").write_text("{partial", encoding="utf-8")
        self.store.refresh(force=True)
        self.assertEqual(self.store.summary()["summary"]["tasks"], 3)

    def test_algorithm_version_invalidates_old_results_without_changing_input_status(self):
        self.document["executionProfile"] = dict(executableSha256="current-algorithm", caseConcurrency=3, workers=4)
        write(self.catalog / "catalog.json", self.document)
        self.result(runSignature=dict(inputSha256="current-input", executableSha256="old-algorithm"))
        task = self.store.task("task-1")["task"]
        self.assertEqual(task["status"], "ready")
        self.assertEqual(task["benchmark"], dict(status="stale-algorithm", reason="算法已更新，等待本次重跑"))
        summary = self.store.summary()
        self.assertEqual(summary["summary"]["executed"], 0)
        self.assertEqual(summary["summary"]["complete"], 0)
        self.assertEqual(summary["summary"]["pending"], 1)
        self.assertEqual(self.store.tasks(filter="executed")["total"], 0)
        self.assertEqual(self.store.tasks(filter="pending")["total"], 1)
        self.result(runSignature=dict(inputSha256="current-input", executableSha256="current-algorithm"))
        self.assertEqual(self.store.summary()["summary"]["executed"], 1)
        self.assertEqual(self.store.summary()["summary"]["complete"], 1)
        self.document["executionProfile"]["executableSha256"] = "next-algorithm"
        write(self.catalog / "catalog.json", self.document)
        self.store.refresh(force=True)
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "stale-algorithm")

    def test_algorithm_hash_fallback_and_input_staleness_precedence(self):
        self.document["executionProfile"] = dict(executableSha256="current-algorithm")
        write(self.catalog / "catalog.json", self.document)
        self.result(benchmark=dict(status="finished", executableSha256="current-algorithm", valid=True, complete=True,
                                   placed=5, required=5, boards=1, firstCompleteWallSeconds=.25, nativeWallSeconds=2.3))
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["firstCompleteWallSeconds"], .25)
        self.assertEqual(self.store.summary()["summary"]["complete"], 1)
        self.result(runSignature=dict(inputSha256="old-input", executableSha256="old-algorithm"))
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "stale-input")

    def test_concurrent_progress_retains_each_task_and_worker_scope(self):
        progress = dict(status="running", currentTasks=["task-1", "task-4", "task-5"], completed=4, total=20,
                        caseConcurrency=3, workers=4, timeLimitSeconds=60)
        write(self.catalog / "progress.json", progress)
        self.store.refresh(force=True)
        self.assertEqual(self.store.summary()["progress"], progress)
        self.assertEqual(self.store.summary()["summary"]["executed"], 0)

    def test_needs_fix_progress_and_finished_partial_are_not_complete(self):
        self.result(benchmark=dict(status="finished", valid=True, complete=False, placed=4, required=5, boards=1))
        progress = dict(status="needs-fix", completed=13, total=13, failed=0, incomplete=1, invalid=0, unresolved=1)
        write(self.catalog / "progress.json", progress)
        self.store.refresh(force=True)
        summary = self.store.summary()
        self.assertEqual(summary["progress"], progress)
        self.assertEqual(summary["summary"]["executed"], 1)
        self.assertEqual(summary["summary"]["complete"], 0)
        self.assertEqual(self.store.task("task-1")["task"]["benchmark"]["status"], "finished")
        self.result(benchmark=dict(status="finished", valid=False, complete=True, placed=5, required=5, boards=1))
        self.assertEqual(self.store.summary()["summary"]["complete"], 0)

    def test_progress_updates_independently_of_catalogue_and_result(self):
        revision = self.store.revision
        write(self.catalog / "progress.json", dict(status="running", currentTask="task-1", completed=0, total=1,
                                                   timeLimitSeconds=60, attempts=8000000, workers=4))
        self.store.refresh(force=True)
        self.assertGreater(self.store.revision, revision)
        self.assertEqual(self.store.summary()["progress"]["currentTask"], "task-1")
        self.assertEqual(self.store.summary()["summary"]["executed"], 0)

    def test_downloads_reject_escape_and_non_dxf_paths(self):
        self.assertEqual(self.store.dxf_file("asset-1"), self.dxf.resolve())
        for value in ("../asset-1", "a/b", "a..b", "a\\b"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.store.asset(value)
        for relative in ("../outside.dxf", "catalog/catalog.json", "DXF/../outside.dxf", "DXF/catalog.json", "C:/outside.dxf"):
            self.document["assets"][0]["relativePath"] = relative
            write(self.catalog / "catalog.json", self.document)
            self.store.refresh(force=True)
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                self.store.dxf_file("asset-1")
        with self.assertRaises(ValueError):
            self.store.csv_file("../catalog.json")
        self.assertTrue(self.store.csv_file("tasks.csv").is_file())

    def request(self, path):
        handler = object.__new__(ViewerHandler)
        handler.command = "GET"
        handler.path = path
        handler.server = SimpleNamespace(store=SimpleNamespace(workspace_root=self.root, files=RecordFiles()))
        handler.wfile = io.BytesIO()
        captured = {}
        handler.send_json = lambda value: captured.update(json=value)
        handler.send_bytes = lambda value, content_type, attachment=None: captured.update(body=value, contentType=content_type, attachment=attachment)
        handler.send_error = lambda code, message="": captured.update(status=code, message=message)
        handler.send_response = lambda code: captured.update(status=code)
        handler.send_header = lambda *_: None
        handler.end_headers = lambda: None
        handler.do_GET()
        return captured

    def test_http_read_only_routes_and_downloads_use_catalogue_ids(self):
        self.assertEqual(self.request("/api/materials/summary")["json"]["summary"]["tasks"], 3)
        self.assertEqual(self.request("/api/materials/tasks?filter=ready&limit=1")["json"]["total"], 1)
        self.assertEqual(self.request("/api/materials/tasks/task-1")["json"]["task"]["id"], "task-1")
        self.assertEqual(self.request("/api/materials/assets/asset-1/dxf")["body"], self.dxf.read_bytes())
        self.assertEqual(self.request("/api/materials/csv/tasks.csv")["attachment"], "tasks.csv")
        self.assertEqual(self.request("/api/materials/csv/pk_results.csv")["attachment"], "pk_results.csv")
        self.assertEqual(self.request("/api/materials/assets/..%2Foutside/dxf")["status"], 503)
        self.assertEqual(self.request("/api/materials/assets/not-found/dxf")["status"], 404)
        self.assertEqual(self.request("/materials.html")["contentType"], "text/html; charset=utf-8")


if __name__ == "__main__":
    unittest.main()
