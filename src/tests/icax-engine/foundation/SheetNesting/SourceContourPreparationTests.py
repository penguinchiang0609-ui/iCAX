"""Original-source preparation facts; never create DXFs or nesting layouts."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[5]
PLANE = ROOT / "TestData/Plane"
sys.path.insert(0, str(ROOT / "src/tools/sheet-nesting"))
from dxf_material import _geos
from dxf_task import straight_contour
from plane_materials_textile import build_tasks, iter_source_contour_preparations


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fingerprint(points):
    shapely, Polygon, _ = _geos()
    from shapely.affinity import translate
    shape = shapely.MultiPolygon([Polygon(points)])
    x, y, *_ = shape.bounds
    return hashlib.sha256(shapely.normalize(translate(shape, xoff=-x, yoff=-y)).wkb).hexdigest()


class SourceContourPreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads((PLANE / "catalog/catalog.json").read_text(encoding="utf-8-sig"))
        # Reconstruct original source/DXF availability, so this regression can
        # still replay the missing-DXF inputs after their preparation is saved.
        by_source = {source: asset["id"] for asset in cls.catalog["assets"] for source in asset["sourcePaths"]}
        cls.original_tasks = build_tasks(PLANE)
        for task in cls.original_tasks:
            task["required"] = sum(part["quantity"] for part in task["parts"])
            task["missingAssets"] = []
            for index, part in enumerate(task["parts"]):
                part["id"] = f"P{index:04d}"
                part["assetId"] = by_source.get(part.get("sourcePath"))
                if not part["assetId"]:
                    task["missingAssets"].append(part.get("requestedDxf") or part["id"])
            task["status"] = "missing-assets" if task["missingAssets"] else "ready"
        cls.test_tasks = deepcopy(cls.original_tasks)
        cls.source_records = {str(row["shape_hash"]): row for row in json.loads(
            (PLANE / "00_metadata_and_mapping/source_shapes.json").read_text(encoding="utf-8-sig"))}
        cls.original_files = [PLANE / "00_metadata_and_mapping" / name for name in
                              ("source_shapes.json", "shape_mapping_validation.json", "jobs_with_quantities.json")]
        cls.current_inputs = [PLANE / task["inputPath"] for task in cls.catalog["tasks"]
                              if task["status"] == "ready"]
        cls.before = {path: digest(path) for path in cls.original_files + cls.current_inputs}
        cls.facts = list(iter_source_contour_preparations(PLANE, cls.test_tasks))

    def setUp(self):
        folder = ROOT / "output/work/sheet-nesting/source-contour-preparation-tests"
        folder.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=folder)
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def fixture(self, aliases=False):
        # A nonzero source origin proves preparation does not normalize it.
        points = [[37, 51], [47, 51], [47, 57], [37, 57], [37, 51]]
        geometry_hash = fingerprint(points)
        records = [dict(shape_hash=101, raw=[value for point in points for value in point], sizes=[10])]
        mapping = [dict(shape_hash=101, valid=True, ring_count=1, hole_count=0,
                        geometry_hash_translation_invariant=geometry_hash, dxf="dxf/shape_101.dxf")]
        demands = [dict(shape_hash=101, quantity=2, valid=True)]
        hashes, quantity = ["101"], 2
        if aliases:
            shifted = [[x + 100, y - 30] for x, y in points]
            records.append(dict(shape_hash=102, raw=[value for point in shifted for value in point], sizes=[10]))
            mapping.append(dict(shape_hash=102, valid=True, ring_count=1, hole_count=0,
                                geometry_hash_translation_invariant=geometry_hash, dxf="dxf/shape_101.dxf"))
            demands.append(dict(shape_hash=102, quantity=3, valid=True))
            hashes, quantity = ["101", "102"], 5
        directory = self.root / "00_metadata_and_mapping"
        directory.mkdir()
        self.files = {"source_shapes.json": records, "shape_mapping_validation.json": mapping,
                      "jobs_with_quantities.json": [dict(tasks_index=0, shape_quantities=demands)]}
        self.save()
        return dict(id="textile-0", collection="textile", status="missing-assets", sourceTaskIndex="0",
                    required=quantity, missingAssets=["dxf/shape_101.dxf"],
                    parts=[dict(id="P0000", quantity=quantity, assetId=None, sourcePath=None,
                                geometryHash=geometry_hash, sourceShapeHashes=hashes)])

    def save(self):
        for name, document in self.files.items():
            (self.root / "00_metadata_and_mapping" / name).write_text(
                json.dumps(document), encoding="utf-8")

    def prepare(self, task):
        return list(iter_source_contour_preparations(self.root, [task]))[0]

    def test_real_missing_tasks_are_complete_source_facts_not_invented_material(self):
        self.assertEqual(len(self.facts), 629)
        ready = [fact for fact in self.facts if fact["status"] == "ready"]
        self.assertEqual(len(ready), 628)
        self.assertEqual(sum(part["quantity"] for fact in ready for part in fact["nativeParts"]), 43973)
        by_id = {task["id"]: task for task in self.original_tasks}
        for fact in ready:
            original = by_id[fact["taskId"]]
            self.assertEqual(len(fact["parts"]), len(fact["nativeParts"]))
            self.assertEqual(sum(p["quantity"] for p in fact["nativeParts"]), original["required"])
            self.assertEqual(fact["missingDxfAssets"], original["missingAssets"])
            self.assertEqual(fact["issues"], [])
            for original_part, part, native in zip(original["parts"], fact["parts"], fact["nativeParts"]):
                self.assertEqual({key: part[key] for key in original_part}, original_part)
                reference = part["geometrySource"]
                self.assertNotIn("outer", reference)
                self.assertEqual(reference["scaleToMm"] if "scaleToMm" in reference else reference["units"]["scaleToMm"], 1)
                self.assertEqual(reference["path"], "00_metadata_and_mapping/source_shapes.json")
                self.assertEqual(reference["sha256"], self.before[self.original_files[0]])
                source = self.source_records[reference["shapeHash"]]
                points = [source["raw"][i:i + 2] for i in range(0, len(source["raw"]), 2)]
                if points[-1] == points[0]:
                    points.pop()
                self.assertEqual(native["geometry"], dict(radius=0, center=[0, 0], outer=points, holes=[]))
                self.assertTrue(all(type(value) is int for point in native["geometry"]["outer"] for value in point))
                self.assertEqual((native["id"], native["quantity"]), (original_part["id"], original_part["quantity"]))
                self.assertEqual((native["freeRotation"], native["allowedAngles"], native["material"]),
                                 (True, [], "pk-material"))
        self.assertEqual(self.test_tasks, self.original_tasks)

    def test_real_source_self_intersection_keeps_evidence_and_no_native_input(self):
        fact = next(fact for fact in self.facts if fact["taskId"] == "textile-69")
        self.assertEqual(fact["status"], "source-invalid")
        self.assertEqual(fact["nativeParts"], [])
        issue = next(item for item in fact["invalidSourceShapes"] if item["shapeHash"] == "-4981381153802229975")
        self.assertIn("5616 4755", issue["sourceReason"])
        self.assertIn("5616 4755", issue["verifiedReason"])
        invalid_part = next(part for part in fact["parts"] if "-4981381153802229975" in part["sourceShapeHashes"])
        self.assertEqual(invalid_part["geometrySource"]["status"], "source-invalid")
        self.assertEqual(invalid_part["geometrySource"]["dxfStatus"], "source-invalid")
        self.assertFalse(invalid_part["geometrySource"]["exact"])

    def test_source_files_and_all_existing_535_native_inputs_are_unchanged(self):
        self.assertGreaterEqual(len(self.current_inputs), 535)
        for path, before in self.before.items():
            self.assertEqual(digest(path), before, str(path))

    def test_twenty_existing_dxfs_exactly_match_source_points_and_their_own_manifest_hash(self):
        for name in ("02_textile_dxf_2500", "03_textile_dxf_2500", "04_textile_dxf_2460",
                     "textile_benchmark_1953_converted_dxf_200_tasks"):
            folder = PLANE / name
            if name.startswith(("02_", "03_", "04_")):
                hashes = {item["path"]: item["sha256"] for item in json.loads(
                    (folder / "package_manifest.json").read_text(encoding="utf-8-sig"))}
            else:
                hashes = {item["dxf"]: item["dxf_sha256"] for item in json.loads(
                    (folder / "shape_mapping_validation.json").read_text(encoding="utf-8-sig")) if item.get("valid")}
            files = sorted((folder / "dxf").glob("*.dxf"))
            for index in (0, len(files) // 4, len(files) // 2, 3 * len(files) // 4, len(files) - 1):
                path = files[index]
                source = self.source_records[path.stem.removeprefix("shape_")]
                points = [source["raw"][i:i + 2] for i in range(0, len(source["raw"]), 2)]
                if points[-1] == points[0]:
                    points.pop()
                before = digest(path)
                self.assertEqual(straight_contour(path), points)
                self.assertEqual(before, hashes[path.relative_to(folder).as_posix()])
                self.assertEqual(digest(path), before)

    def test_translated_aliases_keep_original_representative_and_full_quantity(self):
        task = self.fixture(aliases=True)
        original = deepcopy(task)
        before = {path: digest(path) for path in (self.root / "00_metadata_and_mapping").iterdir()}
        fact = self.prepare(task)
        self.assertEqual(fact["status"], "ready")
        self.assertEqual(fact["nativeParts"][0]["quantity"], 5)
        self.assertEqual(fact["nativeParts"][0]["geometry"]["outer"][0], [37, 51])
        self.assertEqual(fact["parts"][0]["geometrySource"]["sourceShapeHashes"], ["101", "102"])
        self.assertEqual(task, original)
        self.assertEqual(list(self.root.rglob("*.dxf")), [])
        self.assertEqual(list(self.root.rglob("*.problem")), [])
        for path, checksum in before.items():
            self.assertEqual(digest(path), checksum)

    def test_missing_record_is_explicit_and_never_defaults_geometry(self):
        task = self.fixture()
        self.files["source_shapes.json"] = []
        self.save()
        fact = self.prepare(task)
        self.assertEqual(fact["status"], "source-unavailable")
        self.assertEqual(fact["nativeParts"], [])
        self.assertEqual(fact["sourceProvenance"]["path"], "00_metadata_and_mapping/source_shapes.json")
        self.assertEqual(fact["sourceProvenance"]["sha256"], digest(self.root / "00_metadata_and_mapping/source_shapes.json"))
        self.assertTrue(any("Missing original" in issue for issue in fact["issues"]))

    def test_returned_geometry_is_private_to_each_preparation(self):
        task = self.fixture()
        another = deepcopy(task)
        another["id"] = "textile-copy"
        facts = iter_source_contour_preparations(self.root, [task, another])
        first = next(facts)
        first["nativeParts"][0]["geometry"]["outer"][0][0] = 99999
        second = next(facts)
        self.assertEqual(second["nativeParts"][0]["geometry"]["outer"][0], [37, 51])

    def test_changed_coordinates_cannot_reuse_declared_geometry_hash(self):
        task = self.fixture()
        self.files["source_shapes.json"][0]["raw"][2] += 1
        self.save()
        fact = self.prepare(task)
        self.assertEqual(fact["status"], "source-invalid")
        self.assertEqual(fact["nativeParts"], [])
        self.assertTrue(any("fingerprint" in issue for issue in fact["issues"]))

    def test_changed_quantity_cannot_replace_original_demand(self):
        task = self.fixture()
        task["parts"][0]["quantity"] = 3
        task["required"] = 3
        fact = self.prepare(task)
        self.assertEqual(fact["status"], "source-invalid")
        self.assertEqual(fact["nativeParts"], [])
        self.assertTrue(any("quantity differs" in issue for issue in fact["issues"]))

    def test_unequal_alias_shape_cannot_be_grouped_by_the_old_hash(self):
        task = self.fixture(aliases=True)
        self.files["source_shapes.json"][1]["raw"][2] += 1
        points = [self.files["source_shapes.json"][1]["raw"][i:i + 2] for i in range(0, 10, 2)]
        self.files["shape_mapping_validation.json"][1]["geometry_hash_translation_invariant"] = fingerprint(points)
        self.save()
        fact = self.prepare(task)
        self.assertEqual(fact["status"], "source-invalid")
        self.assertEqual(fact["nativeParts"], [])
        self.assertTrue(any("translation-equivalent" in issue for issue in fact["issues"]))

    def test_ready_and_other_collections_do_not_even_open_source_files(self):
        tasks = [dict(id="current", collection="textile", status="ready"),
                 dict(id="other", collection="metal", status="missing-assets")]
        self.assertEqual(list(iter_source_contour_preparations(self.root, tasks)), [])

    def test_missing_source_file_is_a_fact_and_not_an_empty_success(self):
        task = self.fixture()
        (self.root / "00_metadata_and_mapping/source_shapes.json").unlink()
        fact = self.prepare(task)
        self.assertEqual(fact["status"], "source-unavailable")
        self.assertEqual(fact["nativeParts"], [])
        self.assertEqual(fact["sourceProvenance"]["path"], "00_metadata_and_mapping/source_shapes.json")
        self.assertIsNone(fact["sourceProvenance"]["sha256"])


if __name__ == "__main__":
    unittest.main()
