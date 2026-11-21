"""Exercise actual Windows reader locks and retained native evidence."""
import ctypes
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "src/tools/sheet-nesting"))
import plane_materials as publisher


class MaterialsPublishingTests(unittest.TestCase):
    def test_mirror_permissions_migrate_selected_inputs_without_changing_originals(self):
        from export_original_dxf import read_original
        folder=ROOT/"output/work/sheet-nesting/publishing-tests";folder.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as temporary:
            plane=Path(temporary);inputs=plane/"catalog/inputs";inputs.mkdir(parents=True)
            problem=dict(partGap=.2,edgeMargin=.5,tolerance=1e-7,allowCommonEdges=True,
                parts=[dict(id=identity,quantity=quantity,material="m",freeRotation=True,allowedAngles=[],
                    geometry=dict(radius=0,center=[0,0],outer=[[0,0],[7,0],[3,2],[0,5]],holes=[],commonEdgeRings=[True]))
                    for identity,quantity in (("p0",3),("p1",4))],
                stocks=[dict(id="s",width=3000,height=6000,quantity=7,cost=1,material="m")])
            tasks=[];originals={}
            for identity in ("selected","unselected"):
                source=inputs/(identity+".problem");publisher.export(problem,source);originals[identity]=source.read_bytes()
                tasks.append(dict(id=identity,inputPath=source.relative_to(plane).as_posix(),
                    inputSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    parts=[dict(id="p0",quantity=3),dict(id="p1",quantity=4,allowMirror=False)]))
            unselected=copy.deepcopy(tasks[1]);document=dict(tasks=tasks)
            with patch.object(publisher,"save_catalog_views") as save:
                self.assertEqual(publisher.apply_mirror_permissions(plane,document,tasks[:1]),["selected"])
                save.assert_called_once_with(plane,document)
                save.reset_mock()
                self.assertEqual(publisher.apply_mirror_permissions(plane,document,tasks[:1]),[])
                save.assert_not_called()
            self.assertEqual(tasks[1],unselected)
            for identity,content in originals.items():self.assertEqual((inputs/(identity+".problem")).read_bytes(),content)
            after=read_original(plane/tasks[0]["inputPath"])
            self.assertEqual([p.pop("allowMirror") for p in after["parts"]],[True,False])
            before=read_original(inputs/"selected.problem")
            for part in before["parts"]:part.pop("allowMirror")
            self.assertEqual(after,before)
            self.assertEqual(tasks[0]["inputHistory"][0]["inputPath"],"catalog/inputs/selected.problem")
            self.assertEqual(hashlib.sha256((plane/tasks[0]["inputPath"]).read_bytes()).hexdigest(),tasks[0]["inputSha256"])
            (plane/tasks[0]["inputPath"]).write_text("corrupted",encoding="utf-8")
            with self.assertRaisesRegex(ValueError,"source hash"):
                publisher.apply_mirror_permissions(plane,document,tasks[:1])

    def test_mirror_permission_requires_boolean_and_respects_explicit_restrictions(self):
        self.assertTrue(publisher.mirror_permission({}))
        self.assertFalse(publisher.mirror_permission(dict(allowMirror=False)))
        self.assertFalse(publisher.mirror_permission({},dict(rules=dict(allowMirror=False))))
        self.assertTrue(publisher.mirror_permission(dict(allowMirror=True),dict(rules=dict(allowMirror=False))))
        with self.assertRaisesRegex(ValueError,"boolean"):publisher.mirror_permission(dict(allowMirror="false"))
        task=dict(parts=[dict(id="p",quantity=2)])
        signature=publisher.mirror_permissions_signature(task)
        task["parts"][0]["allowMirror"]=False
        self.assertNotEqual(signature,publisher.mirror_permissions_signature(task))

    def test_refresh_does_not_reparse_unrelated_reference_drawings(self):
        folder=ROOT/"output/work/sheet-nesting/publishing-tests";folder.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as temporary:
            plane=Path(temporary)
            data=dict(assets=[dict(id="reference-asset",geometry=dict(status="unsupported"))],
                tasks=[dict(id="drawing",kind="reference",status="reference",parts=[dict(assetId="reference-asset")])])
            publisher.atomic_json(plane/"catalog/catalog.json",data)
            with patch.object(publisher,"import_material") as reader,patch.object(publisher,"save_catalog_views") as save:
                # The final print needs only the already known preparation count.
                data["summary"]={"readyTasks":0}
                publisher.atomic_json(plane/"catalog/catalog.json",data)
                publisher.refresh_imports(plane)
                reader.assert_not_called();save.assert_called_once()

    def test_material_join_retry_does_not_change_strict_success_or_other_failures(self):
        path=Path("source.dxf");imported={"geometries":[],"metadata":{"exact":True}}
        with patch.object(publisher,"import_shape",return_value=imported) as reader:
            self.assertIs(publisher.import_material(path,25.4),imported)
            reader.assert_called_once_with(path,scale_to_mm=25.4,tolerance_mm=.01)
        with patch.object(publisher,"import_shape",side_effect=[ValueError("Open or branching curved LINE/ARC chain: gap"),imported]) as reader:
            self.assertIs(publisher.import_material(path,1),imported)
            self.assertEqual(reader.call_args.kwargs["connection_tolerance_mm"],1e-5)
        with patch.object(publisher,"import_shape",side_effect=ValueError("Unsupported source material")) as reader:
            with self.assertRaisesRegex(ValueError,"Unsupported source material"):
                publisher.import_material(path,1)
            self.assertEqual(reader.call_count,1)

    def test_native_trace_invocation_publication_and_resume_are_bound_together(self):
        folder = ROOT / "output/work/sheet-nesting/publishing-tests"
        folder.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as temporary:
            plane=Path(temporary);catalog=plane/"catalog"
            executable=plane/"native/SheetNestingTask.exe"
            executable.parent.mkdir();executable.write_bytes(b"test-native-invocation")
            task=dict(id="sample",label="sample",collection="test",status="ready",required=1,
                stocks=[dict(widthMm=100,heightMm=100)],inputPath="catalog/inputs/sample.problem",inputSha256="input-hash")
            task["parts"]=[dict(id="p",quantity=1)]
            source=plane/task["inputPath"];source.parent.mkdir(parents=True)
            publisher.export(dict(partGap=.2,edgeMargin=.5,tolerance=1e-7,allowCommonEdges=True,parts=[dict(id="p",quantity=1,material="m",
                freeRotation=True,allowMirror=True,allowedAngles=[],geometry=dict(radius=0,center=[0,0],outer=[[0,0],[2,0],[0,2]],holes=[]))],
                stocks=[dict(id="s",width=100,height=100,quantity=1,cost=1,material="m")]),source)
            task["inputSha256"]=hashlib.sha256(source.read_bytes()).hexdigest()
            publisher.atomic_json(catalog/"catalog.json",dict(tasks=[task]))
            invocations=[]
            def native(arguments,**kwargs):
                invocations.append(arguments)
                raw=Path(arguments[3]);raw.parent.mkdir(parents=True,exist_ok=True)
                payload=dict(wholeJobWallSeconds=.2,firstCompleteWallSeconds=.1,problem=dict(parts=[]),settings={},
                    validation=dict(materialArea=4,errors=[]),result=dict(boards=1,placed=1,complete=True,valid=True,
                        layouts=[dict(width=100,height=100,placements=[])],diagnostics=[]))
                if "--trace" in arguments:
                    index=Path(arguments[-1]);payload["nativeConstructionTrace"]=index.name
                    payload["instrumentedTiming"]=True
                    publisher.atomic_json(index,dict(format="native-construction-jsonl",schemaVersion=2,
                        resultFile=raw.name,problem=payload["problem"],
                        runCompleted=True,observerFailed=False,eventsWritten=3,eventsOmitted=7,truncated=True,routes=[dict(id="route",events=3)]))
                publisher.atomic_json(raw,payload)
                return SimpleNamespace(returncode=0,stdout="",stderr="")
            with patch.object(publisher.subprocess,"run",side_effect=native):
                publisher.run(plane,executable,60,1000,4,0,"","",1,True,only_task="sample")
                result=json.loads((catalog/"results/sample.json").read_text())
                live=json.loads((catalog/"live/sample.json").read_text())
                trace=result["benchmark"]["nativeConstructionTrace"]
                self.assertEqual(invocations[0][-2],"--trace")
                self.assertEqual(trace,live["nativeConstructionTrace"])
                self.assertEqual(trace["runSignature"],result["runSignature"])
                self.assertTrue(result["benchmark"]["constructionTraceSummary"]["truncated"])
                self.assertNotIn("constructionTraceError",result["benchmark"])
                publisher.run(plane,executable,60,1000,4,0,"","",1,True,only_task="sample")
                self.assertEqual(len(invocations),1)
                with self.assertRaisesRegex(ValueError,"Requested task"):
                    publisher.run(plane,executable,60,1000,4,1,"","",1,True,only_task="typo")
                self.assertEqual(len(invocations),1)
                # An unrecorded run is a different invocation, even for the same native/input.
                publisher.run(plane,executable,60,1000,4,0,"","",1,False)
                self.assertEqual(len(invocations),2)
                self.assertNotIn("--trace",invocations[-1])

    def test_native_trace_cannot_point_at_a_different_result(self):
        folder=ROOT/"output/work/sheet-nesting/publishing-tests";folder.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as temporary:
            plane=Path(temporary);raw=plane/"catalog/raw/task.json"
            publisher.atomic_json(raw,dict(wholeJobWallSeconds=.1,problem={},settings={},
                validation=dict(materialArea=1),result=dict(boards=0,placed=0,complete=False,valid=True,layouts=[]),
                nativeConstructionTrace="some-other-task.trace-index.json"))
            with self.assertRaisesRegex(ValueError,"bound to its own raw result"):
                publisher.native_benchmark(dict(required=1,inputSha256="source"),raw,plane,"native",1)

    @unittest.skipUnless(os.name == "nt", "Windows sharing semantics")
    def test_atomic_publish_survives_a_real_concurrent_reader(self):
        folder = ROOT / "output/work/sheet-nesting/publishing-tests"
        folder.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as temporary:
            path = Path(temporary) / "progress.json"
            publisher.atomic_json(path, {"completed": 1})
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.restype = ctypes.c_void_p
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                          ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            # Permit readers, but deny replacement while this reader is open.
            handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
            released = threading.Event()
            def close_reader():
                kernel.CloseHandle(handle)
                released.set()
            timer = threading.Timer(.15, close_reader)
            timer.start()
            try:
                publisher.atomic_json(path, {"completed": 2})
                self.assertTrue(released.is_set())
                self.assertEqual(json.loads(path.read_text()), {"completed": 2})
                self.assertEqual(list(Path(temporary).glob("*.tmp")), [])
            finally:
                timer.join()

    def test_recovered_native_partial_remains_partial_and_keeps_actual_timing(self):
        plane = ROOT / "TestData/Plane"
        raw = plane / "catalog/raw/textile-1-d4151a889003.json"
        if not raw.exists():self.skipTest("Local native evidence not available")
        data = json.loads((plane / "catalog/catalog.json").read_text(encoding="utf-8-sig"))
        task = next(t for t in data["tasks"] if t["id"] == "textile-1")
        result = publisher.native_benchmark(task, raw, plane, "original-executable", 3)
        original = json.loads(raw.read_text(encoding="utf-8-sig"))
        self.assertEqual((result["placed"], result["required"]), (132, 220))
        self.assertFalse(result["complete"])
        self.assertTrue(result["valid"])
        self.assertEqual(result["nativeWallSeconds"], original["wholeJobWallSeconds"])
        self.assertNotIn("firstCompleteWallSeconds", result)
        self.assertNotIn("endToEndWallSeconds", result)
        self.assertEqual(result["problem"], original["problem"])

    def test_resume_cannot_claim_success_with_a_retained_partial_result(self):
        original_plane = ROOT / "TestData/Plane"
        executable = ROOT / "output/work/sheet-nesting/source-profile-fix-20261108/build/Release/SheetNestingTask.exe"
        raw = original_plane / "catalog/raw/textile-1-d4151a889003.json"
        if not raw.exists() or not executable.exists():self.skipTest("Local native evidence/build not available")
        data = json.loads((original_plane / "catalog/catalog.json").read_text(encoding="utf-8-sig"))
        task = next(t for t in data["tasks"] if t["id"] == "textile-1")
        digest = hashlib.sha256(executable.read_bytes()).hexdigest()
        signature = dict(inputSha256=task["inputSha256"], executableSha256=digest,
                         timeLimitSeconds=60, maxPlacementAttempts=8000000, workers=4, caseConcurrency=3,
                         mirrorPermissionPolicy="allow-unless-explicitly-restricted-v1",
                         mirrorPermissionsSha256=publisher.mirror_permissions_signature(task))
        folder = ROOT / "output/work/sheet-nesting/publishing-tests"
        folder.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as temporary:
            plane = Path(temporary)
            catalog = plane / "catalog"
            benchmark = publisher.native_benchmark(task, raw, original_plane, digest, 3)
            publisher.atomic_json(catalog / "catalog.json", dict(tasks=[task]))
            publisher.atomic_json(catalog / "results/textile-1.json",
                                  dict(taskId=task["id"], runSignature=signature, benchmark=benchmark))
            publisher.run(plane, executable, 60, 8000000, 4, 0, "", "", 3)
            progress = json.loads((catalog / "progress.json").read_text())
            self.assertEqual(progress["status"], "needs-fix")
            self.assertEqual((progress["completed"], progress["total"]), (0, 0))
            self.assertEqual(progress["unresolved"], 1)
            self.assertEqual(progress["incomplete"], 1)
            self.assertEqual(progress["invalid"], 0)


if __name__ == "__main__":
    unittest.main()
