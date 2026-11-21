"""Exercise the production task CLI's optional observation without repacking externally."""
from __future__ import annotations
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from dxf_task import export
from export_original_dxf import read_original


class NativeTraceCli(unittest.TestCase):
    executable: Path

    def test_isolate_existing_pure_constructor_without_changing_input(self):
        with tempfile.TemporaryDirectory(prefix="icax-constructor-cli-") as directory:
            root=Path(directory)
            problem=root/"original.problem"
            problem.write_text('ICAX_NESTING_PROBLEM_2 1 1\n0.2 0.5 0.0000001 1\n'
                               '"original" 3 "m" 1 0\n0 0 0 4 0\n'
                               '0 0\n8 0\n8 3\n0 3\n'
                               '"stock" 40 40 100 1 "m"\n',encoding="utf-8")
            source=problem.read_bytes();output=root/"isolated.json";index=root/"isolated.trace-index.json"
            base=[str(self.executable),"--initial",str(problem)]
            result=subprocess.run(base+[str(output),".5","100000","4","--constructor","plane.envelope-initial","--trace",str(index)],
                                  capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            actual=json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(actual["settings"]["isolatedConstructor"],"plane.envelope-initial")
            self.assertEqual(actual["settings"]["ranking"],"utilization")
            self.assertEqual(problem.read_bytes(),source)
            self.assertTrue(actual["validation"]["valid"] and actual["validation"]["complete"])
            self.assertIs(actual["problem"]["parts"][0]["allowMirror"],False)
            for layout in actual["result"]["layouts"]:
                for pose in layout["placements"]:self.assertIs(pose["mirrored"],False)
            metadata=json.loads(index.read_text(encoding="utf-8"))
            self.assertEqual(metadata["problem"],actual["problem"])
            events=[json.loads(line)["event"] for line in (root/metadata["eventsFile"]).read_text(encoding="utf-8").splitlines()]
            self.assertEqual([e["route"] for e in events if e["source"]=="scheduler.route-result"],["plane.envelope-initial"])
            self.assertEqual(sum(e["source"]=="scheduler.final-selection" for e in events),1)
            rejected=subprocess.run(base+[str(root/"refinement.json"),".5","100000","4","--constructor","plane.nfp-all"],
                                    capture_output=True,text=True,timeout=30)
            self.assertNotEqual(rejected.returncode,0)
            self.assertFalse((root/"refinement.json").exists())

    def test_existing_invocation_and_trace_schema(self):
        with tempfile.TemporaryDirectory(prefix="icax-trace-cli-") as directory:
            root = Path(directory)
            problem = root / "original.problem"
            problem.write_text('ICAX_NESTING_PROBLEM_2 1 1\n0.2 0.5 0.0000001 1\n'
                               '"original" 3 "m" 1 0\n0 0 0 6 0\n'
                               '0 0\n8 0\n8 3\n3 3\n3 7\n0 7\n'
                               '"stock" 40 40 100 1 "m"\n', encoding="utf-8")
            plain = root / "plain.json"
            traced = root / "observed.json"
            index = root / "observed.trace-index.json"
            base = [str(self.executable), "--initial", str(problem)]
            for output, extra in ((plain, []), (traced, ["--trace", str(index)])):
                result = subprocess.run(base + [str(output), ".5", "100000", "2"] + extra,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
            original = json.loads(plain.read_text(encoding="utf-8"))
            observed = json.loads(traced.read_text(encoding="utf-8"))
            self.assertNotIn("nativeConstructionTrace", original)
            self.assertEqual(observed["problem"], original["problem"])
            self.assertEqual(observed["validation"]["placed"], 3)
            self.assertTrue(observed["validation"]["valid"])
            self.assertTrue(observed["validation"]["complete"])
            self.assertEqual(observed["nativeConstructionTrace"], index.name)
            self.assertTrue(observed["instrumentedTiming"])
            metadata = json.loads(index.read_text(encoding="utf-8"))
            self.assertEqual(metadata["schemaVersion"], 2)
            self.assertEqual(metadata["format"], "native-construction-jsonl")
            self.assertEqual(metadata["problem"], observed["problem"])
            self.assertEqual(metadata["resultFile"], traced.name)
            self.assertTrue(metadata["runCompleted"])
            self.assertFalse(metadata["observerFailed"])
            events_path = root / metadata["eventsFile"]
            records = [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([r["ordinal"] for r in records], list(range(1, len(records) + 1)))
            self.assertEqual(metadata["eventsWritten"], len(records))
            self.assertLessEqual(events_path.stat().st_size, metadata["byteCap"])
            events = [r["event"] for r in records]
            self.assertTrue(any(e["source"] == "scheduler.final-selection" for e in events))
            self.assertTrue(any(e["source"] == "scheduler.quality" and e["rank"] for e in events))
            quality=[e for e in events if e["source"]=="scheduler.quality"]
            self.assertTrue(any("negativeStockUtilization" in {x["name"] for x in e["rank"]} for e in quality))
            self.assertFalse(any("negativeRegularRemnantArea" in {x["name"] for x in e["rank"]} for e in quality))
            self.assertTrue(any(e["layouts"] for e in events))
            self.assertTrue(any(e["scope"] for e in events))
            self.assertTrue(any(e["stage"] in ("generate", "candidate", "commit") for e in events))
            terminal_routes = {e["route"] for e in events if e["source"] == "scheduler.route-result"}
            for route in metadata["routes"]:
                if route["terminalObserved"]:
                    self.assertIn(route["id"], terminal_routes | {e["route"] for e in events if e["source"] == "scheduler.not-started"})
            again = subprocess.run(base + [str(root / "duplicate.json"), ".5", "100000", "2", "--trace", str(index)],
                                   capture_output=True, text=True, timeout=10)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(json.loads(index.read_text(encoding="utf-8")), metadata)

    def test_v4_roundtrip_requires_real_mirrored_material_and_original_identities(self):
        audit_path=Path(__file__).resolve().parents[2]/"tests/icax-engine/foundation/SheetNesting"
        sys.path.insert(0,str(audit_path))
        from audit_material_native import audit_geometry,normalized_layouts
        with tempfile.TemporaryDirectory(prefix="icax-mirrored-cli-") as directory:
            root=Path(directory);problem=root/"mirror.problem"
            source_text=('ICAX_NESTING_PROBLEM_4 1 1\n.2 .5 .0000001 0\n'
                '"source 01" 2 "m" 0 1 1 45\n0 0 0 3 1\n0 0\n8 8\n0 2\n'
                '4\n.5 1\n.75 1.25\n.75 1.5\n.5 1.25\n2 1 0\n'
                '"stock 01" 14 5 3 1 "m"\n')
            problem.write_text(source_text,encoding="utf-8");before=problem.read_bytes()
            output=root/"mirrored.json";index=root/"mirrored.trace-index.json"
            arguments=[str(self.executable),"--initial",str(problem),str(output),".75","100000","1",
                "--constructor","plane.envelope-initial","--trace",str(index)]
            completed=subprocess.run(arguments,capture_output=True,text=True,timeout=30)
            self.assertEqual(completed.returncode,0,completed.stderr)
            actual=json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(problem.read_bytes(),before)
            self.assertEqual(actual["problem"],read_original(problem))
            source=actual["problem"]["parts"][0]
            self.assertIs(source["allowMirror"],True);self.assertIs(source["freeRotation"],False)
            self.assertEqual(source["allowedAngles"],[45])
            self.assertEqual(source["geometry"]["outer"],[[0,0],[8,8],[0,2]])
            self.assertEqual(source["geometry"]["holes"],[[[.5,1],[.75,1.25],[.75,1.5],[.5,1.25]]])
            self.assertEqual(source["geometry"]["commonEdgeRings"],[True,False])
            self.assertTrue(actual["validation"]["valid"] and actual["validation"]["complete"])
            result=actual["result"]
            poses=[p for board in result["layouts"] for p in board["placements"]]
            self.assertEqual(Counter(p["part"] for p in poses),{"source 01":2})
            self.assertEqual({p["instance"] for p in poses},{0,1})
            self.assertEqual(result["remaining"],[]);self.assertEqual(result["placed"],2)
            for pose in poses:
                self.assertIs(type(pose["mirrored"]),bool);self.assertIs(pose["mirrored"],True)
                self.assertAlmostEqual(pose["angle"],45)
            # Ordinary 45 degrees is too tall for this stock; success cannot
            # come from merely emitting a mirror flag on an unchanged pose.
            self.assertGreater(8*math.sqrt(2),5-2*.5)
            self.assertTrue(audit_geometry(actual["problem"],result)["passed"])
            ordinary=json.loads(json.dumps(result))
            for board in ordinary["layouts"]:
                for pose in board["placements"]:pose["mirrored"]=False
            self.assertFalse(audit_geometry(actual["problem"],ordinary)["passed"])

            metadata=json.loads(index.read_text(encoding="utf-8"))
            self.assertEqual(metadata["problem"],actual["problem"])
            events=[json.loads(line)["event"] for line in (root/metadata["eventsFile"]).read_text(encoding="utf-8").splitlines()]
            finals=[event for event in events if event["source"]=="scheduler.final-selection"]
            self.assertEqual(len(finals),1)
            self.assertEqual(normalized_layouts(finals[0]["layouts"],traced=True),normalized_layouts(result["layouts"]))
            mirrors=[]
            for event in events:
                for member in event["members"]:
                    self.assertIs(type(member["pose"]["mirrored"]),bool)
                    mirrors.append(member["pose"]["mirrored"])
                for board in event["layouts"]:
                    for pose in board["placements"]:self.assertIs(type(pose["pose"]["mirrored"]),bool)
            self.assertIn(True,mirrors)

            roundtrip=root/"roundtrip.problem";export(actual["problem"],roundtrip)
            self.assertTrue(roundtrip.read_text(encoding="utf-8").startswith("ICAX_NESTING_PROBLEM_4"))
            self.assertEqual(read_original(roundtrip),actual["problem"])
            repeated=root/"roundtrip.json"
            repeated_process=subprocess.run([str(self.executable),"--initial",str(roundtrip),str(repeated),
                ".75","100000","1","--constructor","plane.envelope-initial"],capture_output=True,text=True,timeout=30)
            self.assertEqual(repeated_process.returncode,0,repeated_process.stderr)
            repeated_data=json.loads(repeated.read_text(encoding="utf-8"))
            self.assertEqual(repeated_data["problem"],actual["problem"])
            self.assertTrue(repeated_data["validation"]["valid"] and repeated_data["validation"]["complete"])
            self.assertTrue(audit_geometry(repeated_data["problem"],repeated_data["result"])["passed"])

            forbidden=root/"forbidden.problem";forbidden.write_text(source_text.replace('"m" 0 1 1 45','"m" 0 0 1 45'),encoding="utf-8")
            forbidden_output=root/"forbidden.json"
            rejected=subprocess.run([str(self.executable),"--initial",str(forbidden),str(forbidden_output),
                ".75","100000","1","--constructor","plane.envelope-initial"],capture_output=True,text=True,timeout=30)
            self.assertEqual(rejected.returncode,0,rejected.stderr)
            rejected_data=json.loads(forbidden_output.read_text(encoding="utf-8"))
            self.assertIs(rejected_data["problem"]["parts"][0]["allowMirror"],False)
            self.assertTrue(rejected_data["validation"]["valid"])
            self.assertFalse(rejected_data["validation"]["complete"])
            self.assertEqual(rejected_data["validation"]["placed"],0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    NativeTraceCli.executable = args.executable.resolve()
    unittest.main(argv=[__file__, *remaining])
