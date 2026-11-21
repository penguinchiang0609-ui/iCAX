from decimal import Decimal
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from export_original_dxf import export_original, read_original


ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "output/work/sheet-nesting/original-dxf-export-tests"
WORK.mkdir(parents=True, exist_ok=True)


def entities(path):
    """Read raw DXF groups independently of the writer and its entity builder."""
    lines = path.read_text(encoding="utf-8").splitlines()
    if len(lines) % 2:
        raise AssertionError("unpaired DXF groups")
    tags = [(int(lines[i]), lines[i+1]) for i in range(0, len(lines), 2)]
    active, expect_section = False, False
    result = []
    for code, value in tags:
        if (code, value) == (0, "SECTION"):
            expect_section = True
        elif expect_section and code == 2:
            active = value == "ENTITIES"
            expect_section = False
        elif (code, value) == (0, "ENDSEC"):
            active = False
        elif active and code == 0:
            result.append(dict(type=value, groups=[]))
        elif active:
            result[-1]["groups"].append((code, value))
    return result


def points(entity):
    xs = [Decimal(v) for code, v in entity["groups"] if code == 10]
    ys = [Decimal(v) for code, v in entity["groups"] if code == 20]
    return list(zip(xs, ys))


class OriginalDxfExportTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp(dir=WORK)).resolve()

    def tearDown(self):
        if not self.directory.is_relative_to(WORK.resolve()):
            raise AssertionError("unsafe test cleanup")
        shutil.rmtree(self.directory)

    def test_holes_circle_demand_and_exact_translation(self):
        polygon = dict(radius=0, center=[0, 0], outer=[[.125, -.875], [5.125, -.875], [5.125, 3.125], [.125, 3.125]],
                       holes=[[[1.125, .125], [2.125, .125], [2.125, 1.125], [1.125, 1.125]]])
        circle = dict(radius=1.25, center=[-.375, 4.625], outer=[], holes=[])
        source = self.directory / "original.json"
        source.write_text(json.dumps(dict(parts=[dict(id="frame", quantity=2, geometry=polygon),
                                                  dict(id="circle", quantity=1, geometry=circle)])), encoding="utf-8")
        before = source.read_bytes()
        destination = self.directory / "dxf"
        manifest = export_original(source, destination)
        self.assertEqual(source.read_bytes(), before)
        output = entities(destination / "source_parts.dxf")
        self.assertEqual([e["type"] for e in output], ["LWPOLYLINE"]*4+["CIRCLE"])
        self.assertEqual(manifest["export"]["demandCopies"], 3)
        for instance in manifest["instances"]:
            shape = polygon if instance["partID"] == "frame" else circle
            dx, dy = map(Decimal, instance["translation"])
            self.assertEqual(dx, dx.to_integral_value())
            self.assertEqual(dy, dy.to_integral_value())
            selected = output[instance["entityStart"]:instance["entityStart"]+instance["entityCount"]]
            original_rings = [[shape["center"]]] if shape["radius"] else [shape["outer"], *shape["holes"]]
            for entity, ring in zip(selected, original_rings):
                self.assertEqual(points(entity), [(Decimal(str(x))+dx, Decimal(str(y))+dy) for x, y in ring])
                fields = dict(entity["groups"])
                self.assertEqual(fields[8], "0")
                if entity["type"] == "LWPOLYLINE":
                    self.assertEqual(fields[70], "1")
                    self.assertEqual(int(fields[90]), len(ring))
                else:
                    self.assertEqual(Decimal(fields[40]), Decimal("1.25"))
        local = entities(destination / manifest["parts"][0]["file"])
        self.assertEqual(points(local[0]), [(Decimal(str(x)), Decimal(str(y))) for x, y in polygon["outer"]])
        self.assertIn("\n$INSUNITS\n70\n4\n", (destination / "source_parts.dxf").read_text())
        self.assertFalse(any(e["type"] in ("TEXT", "MTEXT", "LINE", "INSERT") for e in output))

    def test_existing_export_is_not_overwritten(self):
        source = self.directory / "input.dat"
        source.write_text("h= 4\nn= 1\nd= 2\nm= 1 0\nv= 3\n0 0\n2 0\n0 2\n")
        destination = self.directory / "dxf"
        export_original(source, destination)
        before = (destination / "source_parts.dxf").read_bytes()
        with self.assertRaises(FileExistsError):
            export_original(source, destination)
        self.assertEqual((destination / "source_parts.dxf").read_bytes(), before)

    def test_problem_versions_and_nonfinite_input(self):
        for version in (1, 2, 3, 4):
            path = self.directory / f"v{version}.problem"
            path.write_text(f'ICAX_NESTING_PROBLEM_{version}\n1 1\n0.2 0.5 0.000001'+(" 1" if version >= 2 else "")+
                '\n"p" 3 "steel" 1'+(" 1" if version == 4 else "")+' 0\n1.5 1 2 0 0\n'+
                ("0\n" if version >= 3 else "")+'"s" 2440 1220 2 1 "steel"\n')
            problem = read_original(path)
            self.assertEqual(problem["parts"][0]["quantity"], 3)
            self.assertEqual(problem["parts"][0]["geometry"]["radius"], 1.5)
            self.assertEqual(problem["parts"][0]["allowMirror"], version == 4)
            self.assertEqual(problem["allowCommonEdges"], version >= 2)
        path = self.directory / "invalid.json"
        path.write_text('{"parts":[{"id":"p","quantity":1,"geometry":{"radius":NaN}}]}')
        with self.assertRaises(ValueError):
            export_original(path, self.directory / "invalid")

    def test_v3_v4_source_rings_masks_quantity_and_mirror_metadata(self):
        outer = [[.125, -.875], [8.125, -.875], [8.125, 2.125], [3.125, 2.125],
                 [3.125, 5.125], [.125, 5.125]]
        hole = [[1.125, .125], [2.125, .125], [2.125, 1.125], [1.125, 1.125]]
        for version in (3, 4):
            with self.subTest(version=version):
                source = self.directory / f"rings-v{version}.problem"
                source.write_text(f'ICAX_NESTING_PROBLEM_{version}\n2 1\n.2 .5 .000001 1\n'+
                    '"asymmetric frame" 2 "steel sheet" 0'+(" 1" if version == 4 else "")+' 2 17 203\n'+
                    '0 0 0 6 1\n'+"\n".join(f"{x} {y}" for x, y in outer)+'\n4\n'+
                    "\n".join(f"{x} {y}" for x, y in hole)+'\n2 1 0\n'+
                    '"circle" 3 "steel sheet" 1'+(" 0" if version == 4 else "")+' 0\n'+
                    '1.5 -.375 4.625 0 0\n0\n"source stock" 3000 6000 9 7.75 "steel sheet"\n',
                    encoding="utf-8")
                before = source.read_bytes()
                problem = read_original(source)
                frame, circle = problem["parts"]
                self.assertEqual((frame["id"], frame["quantity"], frame["allowedAngles"]),
                                 ("asymmetric frame", 2, [17, 203]))
                self.assertEqual(frame["allowMirror"], version == 4)
                self.assertFalse(circle["allowMirror"])
                self.assertEqual(frame["geometry"]["outer"], outer)
                self.assertEqual(frame["geometry"]["holes"], [hole])
                self.assertEqual(frame["geometry"]["commonEdgeRings"], [True, False])
                self.assertEqual(circle["geometry"]["commonEdgeRings"], [])
                self.assertEqual(problem["stocks"], [dict(id="source stock", width=3000, height=6000,
                    quantity=9, cost=7.75, material="steel sheet")])
                destination = self.directory / f"dxf-v{version}"
                export_original(source, destination)
                manifest = json.loads((destination / "manifest.json").read_text(encoding="utf-8"))
                self.assertEqual(source.read_bytes(), before)
                self.assertEqual(manifest["export"]["demandCopies"], 5)
                self.assertEqual(manifest["export"]["entities"], 7)
                self.assertFalse(manifest["export"]["mirrorsApplied"])
                self.assertEqual(manifest["parts"][0]["sourceAllowMirror"], version == 4)
                self.assertFalse(manifest["parts"][1]["sourceAllowMirror"])
                self.assertEqual(manifest["parts"][0]["sourceGeometry"], frame["geometry"])
                local = entities(destination / manifest["parts"][0]["file"])
                self.assertEqual([points(entity) for entity in local],
                    [[(Decimal(str(x)), Decimal(str(y))) for x, y in ring] for ring in (outer, hole)])
                combined = entities(destination / "source_parts.dxf")
                self.assertEqual(len(combined), 7)
                for instance in manifest["instances"]:
                    self.assertFalse(instance["mirror"])
                    self.assertEqual(instance["angleDegrees"], 0)
                    dx, dy = map(Decimal, instance["translation"])
                    rings = (outer, hole) if instance["partID"] == frame["id"] else ([circle["geometry"]["center"]],)
                    chosen = combined[instance["entityStart"]:instance["entityStart"]+instance["entityCount"]]
                    for entity, ring in zip(chosen, rings):
                        self.assertEqual(points(entity), [(Decimal(str(x))+dx, Decimal(str(y))+dy) for x, y in ring])

    def test_json_mirror_permission_does_not_apply_a_layout_reflection(self):
        geometry = dict(radius=0, center=[0, 0], outer=[[2, 3], [9, 3], [4, 8]], holes=[],
                        commonEdgeRings=[True])
        source = self.directory / "mirrored-layout.json"
        source.write_text(json.dumps(dict(problem=dict(parts=[dict(id="handed", quantity=2,
            allowMirror=True, freeRotation=True, allowedAngles=[], geometry=geometry)]),
            layouts=[dict(placements=[dict(partID="handed", pose=dict(x=10, y=20, angle=90, mirrored=True))])])),
            encoding="utf-8")
        destination = self.directory / "json-source-dxf"
        export_original(source, destination)
        manifest = json.loads((destination / "manifest.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["parts"][0]["sourceAllowMirror"])
        self.assertEqual(manifest["parts"][0]["sourceGeometry"], geometry)
        self.assertEqual(manifest["export"]["demandCopies"], 2)
        self.assertFalse(manifest["export"]["mirrorsApplied"])
        self.assertFalse(manifest["export"]["rotationsApplied"])
        local = entities(destination / manifest["parts"][0]["file"])
        self.assertEqual(points(local[0]), [(Decimal(x), Decimal(y)) for x, y in geometry["outer"]])
        self.assertTrue(all(not item["mirror"] and item["angleDegrees"] == 0 for item in manifest["instances"]))


if __name__ == "__main__":
    unittest.main()
