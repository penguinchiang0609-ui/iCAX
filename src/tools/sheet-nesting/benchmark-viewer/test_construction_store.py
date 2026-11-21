"""Native construction reader contracts; no solver is invoked."""
import json
import io
import os
from pathlib import Path
import tempfile
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import json_file_io
from construction_store import ConstructionStore
from materials_store import MaterialsStore
from server import ROOT, ViewerHandler


class ConstructionReaderTests(unittest.TestCase):
    def setUp(self):
        self.test_base = ROOT / "output/work/sheet-nesting/benchmark-viewer-tests"
        self.test_base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=self.test_base)
        self.root = Path(self.temporary.name).resolve()
        self.trace = self.root / "traces"
        self.trace.mkdir()
        self.path = self.trace / "sample.construction.jsonl"
        self.path.write_bytes(b"")
        self.store = ConstructionStore(self.root, self.trace)
        self.index = {"schemaVersion": 2, "format": "native-construction-jsonl",
                      "eventsFile": self.path.name, "resultFile": "sample.json",
                      "problem": {"parts": [], "stocks": []}, "routes": [{"id": "r"}]}
        self.save_index()

    def save_index(self):
        (self.trace / "sample.trace-index.json").write_text(json.dumps(self.index), encoding="utf-8")

    def append(self, ordinal, **event):
        defaults = dict(route="r", scope="trial-a", stage="score", source="native", stateID=0, members=[], layouts=[])
        defaults.update(event)
        value = dict(ordinal=ordinal, event=defaults)
        with self.path.open("ab") as stream:
            stream.write(json.dumps(value).encode()+b"\n")
        return value

    def tearDown(self):
        assert self.root.is_relative_to(self.test_base.resolve()) and self.root != self.test_base.resolve()
        self.temporary.cleanup()

    def test_page_filters_and_seek_use_native_ordinals(self):
        for i in range(1, 51):
            self.append(i, route="a" if i % 2 else "b", stage="score" if i % 3 else "filter")
        page = self.store.page("sample", route="a", stage="score", limit=3, offset=3)
        expected = [i for i in range(1, 51) if i % 2 and i % 3]
        self.assertEqual(page["total"], len(expected))
        self.assertEqual([r["ordinal"] for r in page["events"]], expected[3:6])
        seek = self.store.page("sample", route="a", stage="score", limit=3, ordinal=expected[-1])
        self.assertEqual(seek["focusOffset"], len(expected)-1)
        self.assertLessEqual(len(seek["events"]), 3)

    def test_snapshot_never_uses_different_scope_or_future_or_final_layout(self):
        board = dict(stockID="s", width=10, height=10, placements=[dict(part="p")])
        self.append(1, stage="commit", stateID=7, layouts=[board])
        self.append(2, stateID=7)
        self.append(3, scope="trial-b", stateID=7)
        self.append(4, stateID=8)
        self.append(5, stage="complete", stateID=8, layouts=[board])
        self.assertEqual(self.store.detail("sample", 2)["snapshot"]["ordinal"], 1)
        self.assertIsNone(self.store.detail("sample", 3)["snapshot"])
        self.assertIsNone(self.store.detail("sample", 4)["snapshot"])
        self.assertEqual(self.store.detail("sample", 5)["snapshot"]["mode"], "event")

    def test_empty_native_terminal_overrides_earlier_layout(self):
        self.append(1, stage="selected", layouts=[dict(placements=[{}])])
        self.append(2, stage="complete", source="scheduler.route-result", layouts=[], reason="empty-result")
        terminal = self.store.metadata("sample")["routes"][0]["terminal"]
        self.assertEqual(terminal["ordinal"], 2)
        self.assertEqual(terminal["boards"], 0)
        self.assertEqual(terminal["reason"], "empty-result")

    def test_partial_tail_appends_without_duplicate_or_fake_event(self):
        self.append(1)
        second = json.dumps(dict(ordinal=2,event=dict(route="r",scope="x",stage="generate"))).encode()
        with self.path.open("ab") as stream:
            stream.write(second)
        first = self.store.page("sample")
        self.assertEqual(first["total"], 1)
        self.assertTrue(first["partialTail"])
        with self.path.open("ab") as stream:
            stream.write(b"\n")
        second = self.store.page("sample")
        self.assertEqual(second["total"], 2)
        self.assertFalse(second["partialTail"])
        self.assertEqual([r["ordinal"] for r in second["events"]], [1, 2])

    def test_safe_root_rejects_index_path_escape_and_unsafe_task(self):
        for task in ("../sample", "a/b", "a\\b", "..", "a..b"):
            with self.assertRaises(ValueError):
                self.store.index(task)
        self.index["eventsFile"] = "../secret.jsonl"
        self.save_index()
        with self.assertRaises(ValueError):
            self.store.page("sample")

    def test_decision_preview_declares_full_count_and_files_are_not_modified(self):
        for i in range(1, 76):
            self.append(i, stage="score", source="scheduler.quality")
        before = self.path.read_bytes()
        metadata = self.store.metadata("sample")
        self.assertEqual(metadata["decisionCount"], 75)
        self.assertEqual(len(metadata["decisions"]), 50)
        self.assertEqual(metadata["decisions"][0]["ordinal"], 26)
        self.assertEqual(self.path.read_bytes(), before)

    def test_interruption_requires_matching_sidecar_and_does_not_create_native_terminal(self):
        self.index["runCompleted"] = False
        self.save_index()
        self.append(1, stage="begin", source="strategy.begin")
        evidence = dict(reason="Owned diagnostic stopped for an equivalent implementation fix",
                        stoppedAt="2026-11-09T21:36:38+08:00", completeResultProduced=False)
        (self.trace / "other.interrupted.json").write_text(json.dumps(evidence), encoding="utf-8")
        self.assertIsNone(self.store.metadata("sample")["interruption"])
        sidecar = self.trace / "sample.interrupted.json"
        sidecar.write_text(json.dumps(evidence), encoding="utf-8")
        before = {path: path.read_bytes() for path in self.trace.iterdir()}
        metadata = self.store.metadata("sample")
        self.assertEqual(metadata["interruption"]["reason"], evidence["reason"])
        self.assertEqual(metadata["interruption"]["evidenceFile"], sidecar.name)
        self.assertTrue(metadata["interruption"]["diagnosticOnly"])
        self.assertFalse(metadata["index"]["runCompleted"])
        self.assertEqual(metadata["indexedEvents"], 1)
        self.assertEqual(metadata["decisionCount"], 0)
        self.assertIsNone(metadata["routes"][0]["terminal"])
        self.assertEqual({path: path.read_bytes() for path in self.trace.iterdir()}, before)

    def test_invalid_sidecar_and_completed_native_run_do_not_mark_interrupted(self):
        sidecar = self.trace / "sample.interrupted.json"
        valid = dict(reason="Stopped own diagnostic", stoppedAt="2026-11-09T21:36:38+08:00",
                     completeResultProduced=False)
        for value in ([], {}, dict(valid, completeResultProduced=True), dict(valid, completeResultProduced=0),
                      dict(valid, stoppedAt=""), dict(valid, reason=None)):
            with self.subTest(value=value):
                sidecar.write_text(json.dumps(value), encoding="utf-8")
                self.assertIsNone(self.store.metadata("sample")["interruption"])
        sidecar.write_text('{"reason":', encoding="utf-8")
        self.assertIsNone(self.store.metadata("sample")["interruption"])
        sidecar.write_text(json.dumps(valid), encoding="utf-8")
        self.index["runCompleted"] = True
        self.save_index()
        self.assertIsNone(self.store.metadata("sample")["interruption"])

    def write_run(self, name, started, version="recorded-version"):
        index_path = self.trace / (name + ".trace-index.json")
        value = dict(self.index, resultFile=name+".json", eventsFile=name+".construction.jsonl")
        index_path.write_text(json.dumps(value), encoding="utf-8")
        input_path = self.root / "original.problem"
        run = dict(caseId="original-case", version=version, inputPath=str(input_path),
                   inputSha256="1"*64, executableSha256="2"*64, startedAt=started,
                   arguments=["--initial", str(input_path), str(self.trace / (name+".json")),
                              "60", "100", "4", "--trace", str(index_path)])
        (self.trace / (name+".run.json")).write_text(json.dumps(run), encoding="utf-8")
        return run

    def test_default_uses_bound_run_start_not_filename_or_index_modification(self):
        self.write_run("a-newer", "2026-11-11T13:00:32+08:00", "v-recorded-a")
        self.write_run("z-older", "2026-11-11T13:00:31+08:00", "v-recorded-z")
        os.utime(self.trace / "sample.trace-index.json", (2000000000, 2000000000))
        data = self.store.tasks()
        self.assertEqual(data["defaultTaskId"], "a-newer")
        rows = {row["id"]: row for row in data["tasks"]}
        self.assertEqual(rows["a-newer"]["runInfo"]["version"], "v-recorded-a")
        self.assertIn("运行开始", rows["a-newer"]["label"])
        self.assertEqual(rows["sample"]["runInfo"]["metadataStatus"], "missing")
        self.assertIn("版本未记录", rows["sample"]["label"])
        self.assertIn("索引修改时间", rows["sample"]["label"])

    def test_unbound_sidecar_cannot_claim_version_or_replace_default(self):
        self.write_run("bound", "2026-11-11T13:00:31Z", "actual")
        run = self.write_run("a-unbound", "2099-11-11T13:00:31Z", "wrong")
        run["arguments"][-1] = str(self.trace / "bound.trace-index.json")
        (self.trace / "a-unbound.run.json").write_text(json.dumps(run), encoding="utf-8")
        data = self.store.tasks()
        self.assertEqual(data["defaultTaskId"], "bound")
        row = next(row for row in data["tasks"] if row["id"] == "a-unbound")
        self.assertEqual(row["runInfo"]["metadataStatus"], "unbound")
        self.assertIsNone(row["runInfo"]["version"])
        self.assertNotIn("wrong", row["label"])

    def test_missing_metadata_falls_back_to_explicit_index_timestamp(self):
        newer = self.trace / "a-newer.trace-index.json"
        newer.write_text(json.dumps(self.index), encoding="utf-8")
        os.utime(self.trace / "sample.trace-index.json", (1000, 1000))
        os.utime(newer, (2000, 2000))
        data = self.store.tasks()
        self.assertEqual(data["defaultTaskId"], "a-newer")
        info = next(row for row in data["tasks"] if row["id"] == "a-newer")["runInfo"]
        self.assertEqual(info["timeSource"], "index-modified")
        self.assertEqual(info["timeUnix"], 2000)

    def test_metadata_exposes_only_bound_sidecar_and_does_not_modify_evidence(self):
        self.write_run("sample", "2026-11-11T13:00:31+08:00", "real-version")
        before = {path: path.read_bytes() for path in self.trace.iterdir()}
        metadata = self.store.metadata("sample")
        self.assertEqual(metadata["runInfo"]["version"], "real-version")
        self.assertEqual(metadata["runInfo"]["metadataStatus"], "bound")
        self.assertEqual({path: path.read_bytes() for path in self.trace.iterdir()}, before)

    def path_problem(self):
        self.index["problem"] = dict(edgeMargin=.5, partGap=.2,
            parts=[dict(id="p", quantity=2, geometry=dict(outer=[[0,0],[1,0],[1,1],[0,1]]))],
            stocks=[dict(id="s", width=10, height=10)])
        self.save_index()

    @staticmethod
    def path_board(xs, stock_instance=0, instance_start=0, group=1):
        return dict(stockID="s", stockInstance=stock_instance, width=10, height=10,
                    placements=[dict(part="p", instance=instance_start+i,
                                     pose=dict(x=x, y=.5, angle=0, mirrored=False), group=group)
                                for i,x in enumerate(xs)])

    def path_commit(self, ordinal, state, parent, board, scope="trial-a", retained=False):
        placed=board["placements"][-1]
        return self.append(ordinal, source="construction.path.commit" if retained else "constructor.commit",
                           stage="selected" if retained else "commit", scope=scope, stateID=state, parentStateID=parent,
                           members=[dict(partIndex=0, instance=placed["instance"], pose=placed["pose"], group=placed["group"])],
                           layouts=[board])

    def test_selected_construction_follows_parent_chain_and_excludes_search_sibling(self):
        self.path_problem();first=self.path_board([.5]);chosen=self.path_board([.5,2]);sibling=self.path_board([.5,5])
        self.path_commit(1,1,0,first);self.path_commit(2,2,1,chosen);self.path_commit(3,3,1,sibling)
        self.append(4,source="board.raw-construction",stage="complete",scope="trial-a",stateID=2,layouts=[chosen])
        self.append(5,source="scheduler.route-result",stage="complete",resultStatus=0,layouts=[chosen],
                    elapsedSeconds=99,measures=[dict(name="routeElapsedSeconds",value=3.25)])
        self.append(6,source="scheduler.final-selection",stage="selected",resultStatus=0,layouts=[chosen],elapsedSeconds=100)
        data=self.store.metadata("sample");route=data["routes"][0]
        self.assertEqual(data["selectedRoute"],"r")
        self.assertTrue(route["summary"]["selected"])
        self.assertEqual(route["summary"]["routeElapsedSeconds"],3.25)
        self.assertEqual(route["summary"]["placed"],2)
        self.assertAlmostEqual(route["summary"]["metrics"]["stockUtilization"],.02)
        self.assertAlmostEqual(route["summary"]["metrics"]["usedSectionUtilization"],.1)
        page=self.store.page("sample",route="r",view="construction")
        self.assertEqual([row["ordinal"] for row in page["events"]],[1,2,5])
        self.assertNotIn(3,[row["ordinal"] for row in page["events"]])
        self.assertEqual(self.store.page("sample",route="r")["total"],6)

    def test_retained_selected_commits_are_not_counted_as_a_second_actual_submission(self):
        self.path_problem();first=self.path_board([.5]);chosen=self.path_board([.5,2])
        self.path_commit(1,1,0,first);self.path_commit(2,2,1,chosen)
        self.append(3,source="board.raw-construction",stage="complete",scope="trial-a",stateID=2,layouts=[chosen])
        self.path_commit(4,1,0,first,retained=True);self.path_commit(5,2,1,chosen,retained=True)
        self.append(6,source="construction.path.raw",stage="complete",scope="trial-a",stateID=2,layouts=[chosen])
        self.append(7,source="scheduler.route-result",stage="complete",resultStatus=0,layouts=[chosen])
        data=self.store.page("sample",route="r",view="construction")
        self.assertEqual([row["ordinal"] for row in data["events"]],[4,5,7])
        self.assertEqual(data["constructionStatus"],"complete")
        self.assertEqual(self.store.metadata("sample")["routes"][0]["summary"]["placed"],2)

    def test_missing_or_changed_parent_does_not_fabricate_a_selected_history(self):
        self.path_problem();first=self.path_board([.5]);changed=self.path_board([1,2])
        self.path_commit(1,1,0,first);self.path_commit(2,2,1,changed)
        self.append(3,source="board.raw-construction",stage="complete",scope="trial-a",stateID=2,layouts=[changed])
        self.append(4,source="scheduler.route-result",stage="complete",resultStatus=0,layouts=[changed])
        data=self.store.page("sample",route="r",view="construction")
        self.assertEqual(data["events"],[])
        self.assertEqual(data["constructionStatus"],"unavailable")
        branch=self.store.metadata("sample")["routes"][0]["construction"]["branches"][0]
        self.assertEqual(branch["status"],"partial")

    def test_random_winning_scope_resolves_identical_restart_layouts_without_guessing_last(self):
        self.path_problem();board=self.path_board([.5])
        self.path_commit(1,1,0,board,scope="restart-a")
        self.append(2,source="board.raw-construction",stage="complete",scope="restart-a",stateID=1,layouts=[board])
        self.path_commit(3,1,0,board,scope="restart-b")
        self.append(4,source="board.raw-construction",stage="complete",scope="restart-b",stateID=1,layouts=[board])
        self.append(5,source="scheduler.route-result",stage="complete",resultStatus=1,layouts=[board])
        self.assertEqual(self.store.page("sample",route="r",view="construction")["constructionStatus"],"ambiguous")
        self.append(6,source="strategy.construction-selection",stage="selected",scope="restart-a",layouts=[board])
        self.append(7,source="scheduler.route-result",stage="complete",resultStatus=1,layouts=[board])
        self.assertEqual([row["ordinal"] for row in self.store.page("sample",route="r",view="construction")["events"]],[1,7])

    def test_native_promotion_and_whole_board_copy_preserve_actual_mapping_steps(self):
        self.path_problem();source=self.path_board([.5]);promoted=self.path_board([.5],instance_start=5,group=20)
        self.path_commit(1,1,0,source,scope="competitor-a")
        self.append(2,source="board.raw-construction",stage="complete",scope="competitor-a",stateID=1,layouts=[source])
        mapping=[dict(name="sourceLayoutIndex",value=0),dict(name="targetLayoutIndex",value=1)]
        self.append(3,source="competition.board-selection",stage="selected",scope="competitor-a",
                    layouts=[source,promoted],measures=mapping)
        copied=self.path_board([.5],stock_instance=1,instance_start=6,group=21)
        self.append(4,source="sheet-template.copy",stage="commit",stateID=0,layouts=[promoted,copied],measures=mapping)
        self.append(5,source="scheduler.route-result",stage="complete",resultStatus=0,layouts=[promoted,copied])
        page=self.store.page("sample",route="r",view="construction")
        self.assertEqual([row["ordinal"] for row in page["events"]],[1,3,4,5])
        self.assertEqual(page["constructionStatus"],"complete")
        self.assertEqual([row["source"] for row in page["events"]].count("sheet-template.copy"),1)

    def test_actual_applicability_is_reported_separately_from_partial_results(self):
        self.path_problem()
        self.append(1,route="rectangle-only",source="scheduler.applicability",stage="filter",accepted=False,
                    reason="requires-hole-free-rectangular-demand")
        route=next(row for row in self.store.metadata("sample")["routes"] if row["id"]=="rectangle-only")
        self.assertEqual(route["summary"]["status"],"not-applicable")
        self.assertFalse(route["summary"]["applicable"])

    def test_intermediate_raw_and_retained_path_are_never_route_terminal_results(self):
        self.path_problem();self.index["runCompleted"]=False;self.save_index()
        board=self.path_board([.5]);self.path_commit(1,1,0,board)
        self.append(2,source="board.raw-construction",stage="complete",scope="trial-a",stateID=1,layouts=[board])
        self.append(3,source="construction.path.raw",stage="complete",scope="trial-a",stateID=1,layouts=[board])
        route=self.store.metadata("sample")["routes"][0]
        self.assertIsNone(route["terminal"])
        self.assertEqual(route["summary"]["status"],"running")
        self.assertIsNone(route["summary"]["placed"])
        self.assertIsNone(route["summary"]["metrics"]["stockUtilization"])
        self.assertEqual(route["construction"]["status"],"unavailable")
        self.assertEqual(len(route["construction"]["branches"]),1)


class MaterialConstructionReaderTests(unittest.TestCase):
    def setUp(self):
        self.test_base = ROOT / "output/work/sheet-nesting/benchmark-viewer-tests"
        self.test_base.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=self.test_base)
        self.root = Path(self.temporary.name).resolve()
        self.catalog = self.root / "TestData/Plane/catalog"
        self.raw = self.catalog / "raw/current.json"
        self.raw.parent.mkdir(parents=True)
        self.raw.write_text('{"originalFinalLayout":true}', encoding="utf-8")
        self.events = self.raw.with_suffix(".construction.jsonl")
        self.index_path = self.raw.with_suffix(".trace-index.json")
        self.events.write_bytes(b"")
        self.signature = dict(inputSha256="input-1", executableSha256="exe-1", timeLimitSeconds=60, workers=4,
                              caseConcurrency=4, maxPlacementAttempts=8000000, constructionTraceFormat="native-construction-jsonl-v2")
        self.descriptor = dict(indexPath="catalog/raw/current.trace-index.json", inputSha256="input-1",
                               executableSha256="exe-1", runSignature=self.signature.copy())
        self.document = dict(executionProfile={key: value for key, value in self.signature.items() if key != "inputSha256"}, assets=[],
                             tasks=[dict(id="material-1", label="Current material", status="ready", inputSha256="input-1", parts=[])])
        self.write(self.catalog / "catalog.json", self.document)
        self.record = dict(taskId="material-1", runSignature=self.signature.copy(), benchmark=dict(status="finished",
                           rawPath="catalog/raw/current.json", nativeConstructionTrace=self.descriptor))
        self.write(self.catalog / "results/material-1.json", self.record)
        self.index = dict(schemaVersion=2, format="native-construction-jsonl", resultFile=self.raw.name, eventsFile=self.events.name,
                          problem=dict(parts=[dict(id="p", quantity=1)], stocks=[]), routes=[dict(id="r")], runCompleted=False)
        self.write(self.index_path, self.index)
        self.materials = MaterialsStore(self.root)
        self.store = ConstructionStore(self.root, materials=self.materials)

    def tearDown(self):
        assert self.root.is_relative_to(self.test_base.resolve())
        self.temporary.cleanup()

    @staticmethod
    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")

    def append(self, ordinal, **event):
        defaults = dict(route="r", scope="a", stage="commit", source="native", stateID=ordinal, layouts=[])
        defaults.update(event)
        with self.events.open("ab") as stream:
            stream.write(json.dumps(dict(ordinal=ordinal, event=defaults)).encode()+b"\n")

    def test_bound_material_start_time_is_used_without_guessing_a_sidecar_version(self):
        live=dict(taskId="material-1",active=False,runSignature=self.signature.copy(),startedAtUnix=1794441367,
                  rawPath="catalog/raw/current.json",nativeConstructionTrace=self.descriptor.copy())
        self.write(self.catalog/"live/material-1.json",live)
        data=self.store.metadata("material-1",material=True)
        self.assertEqual(data["runInfo"]["metadataStatus"],"bound")
        self.assertEqual(data["runInfo"]["metadataSource"],"material-run")
        self.assertEqual(data["runInfo"]["timeUnix"],1794441367)
        self.assertEqual(data["runInfo"]["executableSha256"],"exe-1")
        self.assertIsNone(data["runInfo"]["version"])
        self.assertEqual(self.store.tasks()["defaultTaskId"],"material::material-1")
        live["rawPath"]="catalog/raw/different.json";self.write(self.catalog/"live/material-1.json",live)
        self.assertNotEqual(self.store.metadata("material-1",material=True)["runInfo"]["metadataStatus"],"bound")

    def test_material_binding_routes_snapshots_scores_and_original_evidence(self):
        board = dict(width=10, height=10, placements=[dict(part="p", x=1, y=2, angle=0)])
        self.append(1, layouts=[board], rank=[dict(name="unplaced", value=0)])
        self.append(2, source="scheduler.route-result", stage="complete", layouts=[board], rank=[dict(name="boards", value=1)])
        self.append(3, route="plane.initial", stage="selected", source="scheduler.final-selection", layouts=[board], reason="selected=r")
        rows = self.store.tasks()["tasks"]
        self.assertEqual(rows[0]["materialTask"], "material-1")
        detail = self.store.metadata("material-1", material=True)
        self.assertTrue(detail["available"])
        self.assertEqual(detail["binding"]["runSignature"], self.signature)
        self.assertEqual(detail["indexedEvents"], 3)
        self.assertEqual(detail["decisionCount"], 2)
        self.assertEqual(self.store.page("material-1", material=True, route="r")["total"], 2)
        event = self.store.detail("material-1", 2, material=True)
        self.assertEqual(event["snapshot"]["layouts"], [board])
        self.assertEqual(event["event"]["rank"], [dict(name="boards", value=1)])
        self.assertEqual(self.store.evidence("material-1", "result", material=True), self.raw.read_bytes())

    def test_material_interruption_uses_bound_raw_stem_without_changing_index(self):
        evidence = dict(reason="Stopped own diagnostic", stoppedAt="2026-11-09T21:36:38+08:00",
                        completeResultProduced=False)
        self.write(self.raw.with_name("material-1.interrupted.json"), evidence)
        self.assertIsNone(self.store.metadata("material-1", material=True)["interruption"])
        self.write(self.raw.with_suffix(".interrupted.json"), evidence)
        before = self.index_path.read_bytes()
        metadata = self.store.metadata("material-1", material=True)
        self.assertEqual(metadata["interruption"]["evidenceFile"], "current.interrupted.json")
        self.assertFalse(metadata["index"]["runCompleted"])
        self.assertEqual(metadata["indexedEvents"], 0)
        self.assertEqual(self.index_path.read_bytes(), before)

    def test_missing_trace_does_not_use_any_other_task_or_final_layout(self):
        self.record["benchmark"].pop("nativeConstructionTrace")
        self.write(self.catalog / "results/material-1.json", self.record)
        metadata = self.store.metadata("material-1", material=True)
        self.assertFalse(metadata["available"])
        self.assertEqual(metadata["indexedEvents"], 0)
        self.assertEqual(metadata["routes"], [])
        self.assertEqual(self.store.tasks(material_task="material-1")["tasks"][0]["status"], "missing")
        with self.assertRaises(FileNotFoundError):
            self.store.detail("material-1", 1, material=True)

    def test_input_algorithm_full_run_signature_and_paths_are_all_bound(self):
        original = json.loads(json.dumps(self.record))
        edits = [lambda value: value["runSignature"].update(inputSha256="old"),
                 lambda value: value["runSignature"].update(executableSha256="old"),
                 lambda value: value["runSignature"].update(workers=2),
                 lambda value: value["benchmark"]["nativeConstructionTrace"]["runSignature"].update(caseConcurrency=1),
                 lambda value: value["benchmark"]["nativeConstructionTrace"].update(inputSha256="old"),
                 lambda value: value["benchmark"]["nativeConstructionTrace"].update(indexPath="catalog/raw/../outside.trace-index.json"),
                 lambda value: value["benchmark"]["nativeConstructionTrace"].update(indexPath="catalog/raw/different.trace-index.json"),
                 lambda value: value["benchmark"].update(rawPath="catalog/catalog.json")]
        for edit in edits:
            with self.subTest(edit=edit):
                value = json.loads(json.dumps(original));edit(value)
                self.write(self.catalog / "results/material-1.json", value)
                metadata = self.store.metadata("material-1", material=True)
                self.assertFalse(metadata["available"])
                self.assertEqual(metadata["binding"]["status"], "binding-error")
                self.assertEqual(metadata["indexedEvents"], 0)

    def test_index_cannot_redirect_to_another_output_or_event_file(self):
        for changes in (dict(eventsFile="../other.jsonl"), dict(eventsFile="other.construction.jsonl"),
                        dict(resultFile="other.json"), dict(inputSha256="old")):
            with self.subTest(changes=changes):
                self.write(self.index_path, dict(self.index, **changes))
                with self.assertRaises(ValueError):
                    self.store.page("material-1", material=True)

    def test_observation_error_without_descriptor_is_separate_and_bound(self):
        self.record["benchmark"].pop("nativeConstructionTrace")
        self.record["benchmark"]["constructionTraceError"]="native observer write failed"
        self.write(self.catalog / "results/material-1.json",self.record)
        metadata=self.store.metadata("material-1",material=True)
        self.assertFalse(metadata["available"])
        self.assertEqual(metadata["binding"]["status"],"observation-error")
        self.assertIn("native observer write failed",metadata["binding"]["reason"])
        self.record["taskId"]="other-task"
        self.write(self.catalog / "results/material-1.json",self.record)
        self.assertEqual(self.store.metadata("material-1",material=True)["binding"]["status"],"binding-error")

    def test_live_before_first_incumbent_grows_and_keeps_sampling_flags(self):
        live = dict(taskId="material-1", active=True, runSignature=self.signature.copy(),
                    rawPath="catalog/raw/current.json", nativeConstructionTrace=self.descriptor)
        self.write(self.catalog / "live/material-1.json", live)
        self.append(1, layouts=[dict(placements=[dict(part="p")])])
        first = self.store.metadata("material-1", material=True)
        self.assertEqual(first["binding"]["status"], "recording")
        self.assertFalse(first["available"])
        self.assertEqual(first["indexedEvents"], 0)
        self.append(2, stage="complete", source="scheduler.route-result", rank=[dict(name="boards", value=1)])
        self.index.update(runCompleted=True, truncated=True, eventsOmitted=10, detailEventsOmitted=8,
                          progressEventsOmitted=2, terminalSnapshotsTruncated=1,
                          routes=[dict(id="r", eventsSeen=12, eventsOmitted=10, terminalSnapshotsTruncated=1)])
        self.write(self.index_path, self.index)
        live["active"] = False
        self.write(self.catalog / "live/material-1.json", live)
        grown = self.store.metadata("material-1", material=True)
        self.assertEqual(grown["indexedEvents"], 2)
        self.assertTrue(grown["index"]["truncated"])
        self.assertTrue(grown["routes"][0]["terminalSnapshotTruncated"])
        self.assertEqual(grown["routes"][0]["eventsOmitted"], 10)
        self.assertIsNone(self.store.detail("material-1", 2, material=True)["snapshot"])

    def test_active_run_blocks_index_stat_resolve_and_all_read_endpoints_during_replacement(self):
        live = dict(taskId="material-1", active=True, runSignature=self.signature.copy(),
                    rawPath="catalog/raw/current.json", nativeConstructionTrace=self.descriptor)
        self.write(self.catalog / "live/material-1.json", live)
        native_stat, native_resolve, native_open = Path.stat, Path.resolve, Path.open
        native_json = json_file_io.open_json_snapshot
        touched, writer_errors = [], []
        start = threading.Event()
        def guarded(native):
            def call(path, *args, **kwargs):
                if path == self.index_path:
                    touched.append(str(path))
                    raise AssertionError("active producer index touched")
                return native(path, *args, **kwargs)
            return call
        def producer():
            start.wait()
            for generation in range(300):
                replacement = self.index_path.with_suffix(".next")
                replacement.write_text(json.dumps(dict(self.index, generation=generation)), encoding="utf-8")
                try:
                    os.replace(replacement, self.index_path)
                except OSError as error:
                    writer_errors.append(str(error))
        def request(path):
            handler = object.__new__(ViewerHandler)
            handler.command = "GET"; handler.path = path; handler.wfile = io.BytesIO()
            handler.server = SimpleNamespace(store=SimpleNamespace(workspace_root=self.root, files=self.materials.files),
                                             materials_store=self.materials, construction_store=self.store)
            captured = {}
            handler.send_json = lambda value: captured.update(json=value)
            handler.send_bytes = lambda value, content_type, attachment=None: captured.update(body=value)
            handler.send_error = lambda code, message="": captured.update(status=code)
            handler.do_GET()
            return captured
        writer = threading.Thread(target=producer)
        with patch.object(Path, "stat", guarded(native_stat)), patch.object(Path, "resolve", guarded(native_resolve)), \
                patch.object(Path, "open", guarded(native_open)), \
                patch("json_file_io.open_json_snapshot", guarded(native_json)):
            writer.start()
            start.set()
            for _ in range(50):
                metadata = request("/api/construction/materials/material-1")["json"]
                self.assertFalse(metadata["available"])
                self.assertEqual(metadata["binding"]["status"], "recording")
                self.assertEqual(request("/api/construction/tasks?materialTask=material-1")["json"]["tasks"][0]["status"], "recording")
                for suffix in ("events", "events/1", "index", "result"):
                    self.assertEqual(request("/api/construction/materials/material-1/" + suffix)["status"], 404)
            writer.join(timeout=15)
        self.assertFalse(writer.is_alive())
        self.assertFalse(touched)
        self.assertFalse(writer_errors, writer_errors[:3])
        self.assertEqual(json.loads(self.index_path.read_bytes())["generation"], 299)
        print("Active index isolation stress: 300 replacements, 300 endpoint checks, 0 index touches, 0 writer errors")

    def test_material_http_routes_use_the_same_bound_trace(self):
        self.append(1, stage="score", rank=[dict(name="boards", value=1)])
        def request(path):
            handler = object.__new__(ViewerHandler)
            handler.command="GET";handler.path=path;handler.wfile=io.BytesIO()
            handler.server=SimpleNamespace(store=SimpleNamespace(workspace_root=self.root, files=self.materials.files),
                                           materials_store=self.materials, construction_store=self.store)
            captured={}
            handler.send_json=lambda value:captured.update(json=value)
            handler.send_bytes=lambda value, content_type, attachment=None:captured.update(body=value)
            handler.send_error=lambda code, message="":captured.update(status=code)
            handler.do_GET();return captured
        self.assertEqual(request("/api/construction/tasks?materialTask=material-1")["json"]["tasks"][0]["materialTask"],"material-1")
        self.assertEqual(request("/api/construction/materials/material-1")["json"]["indexedEvents"],1)
        self.assertEqual(request("/api/construction/materials/material-1/events?stage=score")["json"]["total"],1)
        self.assertEqual(request("/api/construction/materials/material-1/events/1")["json"]["event"]["rank"],[dict(name="boards",value=1)])
        self.assertEqual(request("/api/construction/materials/material-1/result")["body"],self.raw.read_bytes())


if __name__ == "__main__":
    unittest.main()

