"""Independent safety controls for source-bound machining-role metadata.

These fixtures exercise the production DXF importer. They do not construct
placements, alter original drawings, or infer production roles from shape.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[5]
TOOLS = ROOT / "src/tools/sheet-nesting"
spec = importlib.util.spec_from_file_location("semantics_dxf_task", TOOLS / "dxf_task.py")
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


def entity(kind, handle, tags):
    return f"0\n{kind}\n5\n{handle}\n8\n0\n" + tags


def rectangle(handle, x, y, width, height, *, legacy=False):
    points = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
    if not legacy:
        return entity("LWPOLYLINE", handle, "70\n1\n90\n4\n" + "".join(
            f"10\n{a}\n20\n{b}\n" for a, b in points))
    text = entity("POLYLINE", handle, "70\n1\n")
    text += "".join(entity("VERTEX", f"{int(handle, 16) + i + 1:X}",
                           f"10\n{a}\n20\n{b}\n30\n0\n")
                    for i, (a, b) in enumerate(points))
    return text + entity("SEQEND", f"{int(handle, 16) + 5:X}", "")


def circle(handle, x, y, radius):
    return entity("CIRCLE", handle, f"10\n{x}\n20\n{y}\n30\n0\n40\n{radius}\n")


def line(handle, a, b):
    return entity("LINE", handle,
                  f"10\n{a[0]}\n20\n{a[1]}\n30\n0\n11\n{b[0]}\n21\n{b[1]}\n31\n0\n")


def insert(handle, name, x=0, *, columns=1, spacing=120):
    return entity("INSERT", handle,
                  f"2\n{name}\n10\n{x}\n20\n0\n30\n0\n70\n{columns}\n71\n1\n44\n{spacing}\n45\n0\n")


def block(name, contents):
    return f"0\nBLOCK\n2\n{name}\n70\n0\n10\n0\n20\n0\n30\n0\n" + contents + "0\nENDBLK\n"


def drawing(contents, blocks=""):
    head = "0\nSECTION\n2\nBLOCKS\n" + blocks + "0\nENDSEC\n" if blocks else ""
    return head + "0\nSECTION\n2\nENTITIES\n" + contents + "0\nENDSEC\n0\nEOF\n"


def drawing_with_center_linetype(contents):
    tables = ("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n1\n"
              "0\nLTYPE\n2\nCENTERX2\n3\nCenter (2x)\n72\n65\n73\n4\n40\n20.32\n"
              "49\n12.7\n49\n-2.54\n49\n2.54\n49\n-2.54\n0\nENDTAB\n"
              "0\nTABLE\n2\nLAYER\n70\n1\n0\nLAYER\n2\n0\n70\n0\n6\nCONTINUOUS\n"
              "0\nENDTAB\n0\nENDSEC\n")
    return tables + drawing(contents)


def selector(handle, kind="CIRCLE", path=None):
    return {"handle": handle, "type": kind, "instancePath": [] if path is None else path}


def step(handle, column=0, row=0):
    return {"insertHandle": handle, "row": row, "column": column}


def ring_area(points):
    return abs(sum(a[0] * b[1] - a[1] * b[0]
                   for a, b in zip(points, points[1:] + points[:1]))) / 2


class MaterialSemanticsSafety(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = (ROOT / "output/work/sheet-nesting/material-semantics-safety").resolve()
        cls.workspace.mkdir(parents=True, exist_ok=True)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=self.workspace)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.evidence = self.directory / "source-instructions.txt"
        self.evidence.write_text("Circle 20 is engraving only. Circle 30 is a through cut.\n", encoding="utf-8")
        self.source = self.directory / "source.dxf"
        self.source.write_text(drawing(rectangle("10", 0, 0, 100, 100) +
                                       circle("20", 25, 25, 8) + circle("30", 70, 70, 5)), encoding="utf-8")

    def semantics(self, targets=None, *, components=1, holes=1, topology="closed-contours"):
        return {
            "schemaVersion": 1,
            "sourceDxfSha256": hashlib.sha256(self.source.read_bytes()).hexdigest(),
            "evidence": [{"id": "source-note", "kind": "source-instruction", "path": str(self.evidence),
                          "sha256": hashlib.sha256(self.evidence.read_bytes()).hexdigest(),
                          "locator": "line 1", "quote": "Circle 20 is engraving only."}],
            "features": [{"id": "engraving", "role": "non-through-machining", "operation": "engraving",
                          "topology": topology, "entities": targets or [selector("20")],
                          "evidence": ["source-note"]}],
            "expectations": {"materialComponents": components, "throughHoles": holes},
        }

    def run_import(self, metadata=None):
        return adapter.import_shape(self.source, tolerance_mm=.01, material_semantics=metadata)

    def test_bound_role_removes_only_the_named_engraving_and_preserves_true_hole(self):
        original = self.source.read_bytes()
        baseline = adapter.import_shape(self.source, tolerance_mm=.01)
        self.assertEqual(len(baseline["geometry"]["holes"]), 2)
        result = self.run_import(self.semantics())
        self.assertEqual(len(result["geometries"]), 1)
        holes = result["geometry"]["holes"]
        self.assertEqual(len(holes), 1)
        self.assertTrue(all(64.99 <= p[0] <= 75.01 and 64.99 <= p[1] <= 75.01 for p in holes[0]))
        self.assertEqual(original, self.source.read_bytes())

    def test_wrong_source_sha_cannot_apply_even_when_all_handles_match(self):
        metadata = self.semantics()
        metadata["sourceDxfSha256"] = "0" * 64
        with self.assertRaises(ValueError):
            self.run_import(metadata)

    def test_changed_evidence_bytes_cannot_reuse_previous_claim(self):
        metadata = self.semantics()
        self.evidence.write_text("Changed evidence\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.run_import(metadata)

    def test_unknown_handle_or_wrong_entity_type_cannot_be_silently_skipped(self):
        for target in (selector("FFFF"), selector("20", "ARC")):
            with self.subTest(target=target), self.assertRaises(ValueError):
                self.run_import(self.semantics([target]))

    def test_conflicting_process_and_material_roles_cannot_share_one_entity(self):
        metadata = self.semantics()
        feature = deepcopy(metadata["features"][0])
        feature.update(id="true-hole", role="through-hole")
        feature.pop("operation")
        metadata["features"].append(feature)
        with self.assertRaises(ValueError):
            self.run_import(metadata)

    def test_duplicate_selector_cannot_be_used_as_a_second_entity(self):
        with self.assertRaises(ValueError):
            self.run_import(self.semantics([selector("20"), selector("20")]))

    def test_unknown_schema_fields_cannot_supply_replacement_geometry(self):
        for where in ("root", "feature", "selector"):
            metadata = self.semantics()
            target = metadata if where == "root" else metadata["features"][0]
            if where == "selector":
                target = target["entities"][0]
            target["points"] = [[0, 0], [1, 0], [1, 1]]
            with self.subTest(where=where), self.assertRaises(ValueError):
                self.run_import(metadata)

    def test_expectations_validate_the_result_instead_of_changing_it(self):
        for key, value in (("materialComponents", 2), ("throughHoles", 0)):
            metadata = self.semantics()
            metadata["expectations"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.run_import(metadata)

    def test_invalid_role_depth_and_boolean_counts_cannot_be_accepted_as_numbers(self):
        for depth in (-1, float("nan"), float("inf"), True):
            metadata = self.semantics()
            metadata["features"][0]["sourceDepthMm"] = depth
            with self.subTest(depth=depth), self.assertRaises(ValueError):
                self.run_import(metadata)
        for key in ("materialComponents", "throughHoles"):
            metadata = self.semantics()
            metadata["expectations"][key] = True
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.run_import(metadata)

    def test_unknown_evidence_reference_does_not_partially_apply_a_feature(self):
        metadata = self.semantics()
        metadata["features"][0]["evidence"] = ["missing-note"]
        with self.assertRaises(ValueError):
            self.run_import(metadata)

    def test_declared_material_roles_must_match_the_actual_retained_contour(self):
        for role, target in (("through-hole", selector("10", "LWPOLYLINE")),
                             ("outer-profile", selector("30"))):
            metadata = self.semantics()
            metadata["features"].append({"id": "wrong-material-role", "role": role,
                                         "topology": "closed-contours", "entities": [target],
                                         "evidence": ["source-note"]})
            with self.subTest(role=role), self.assertRaises(ValueError):
                self.run_import(metadata)

    def test_partial_closed_feature_does_not_fill_a_true_cut_ring(self):
        points = [(20, 20), (40, 20), (40, 40), (20, 40)]
        edges = "".join(line(f"{0x20 + i:X}", a, b)
                        for i, (a, b) in enumerate(zip(points, points[1:] + points[:1])))
        self.source.write_text(drawing(rectangle("10", 0, 0, 100, 100) + edges), encoding="utf-8")
        targets = [selector(f"{0x20 + i:X}", "LINE") for i in range(3)]
        for topology in ("closed-contours", "open-strokes"):
            with self.subTest(topology=topology), self.assertRaises(ValueError):
                self.run_import(self.semantics(targets, holes=0, topology=topology))

    def test_explicit_open_role_and_inferred_centerlines_cannot_together_fill_a_cut_ring(self):
        for gap in (0., .000005):
            declared = line("20", (20, 20), (40, 20))
            inferred = (line("21", (40 + gap, 20), (40, 40)) +
                        line("22", (40, 40), (20, 40)) + line("23", (20, 40), (20, 20)))
            inferred = inferred.replace("8\n0\n", "8\n0\n6\nCENTERX2\n")
            self.source.write_text(drawing_with_center_linetype(
                rectangle("10", 0, 0, 100, 100) + declared + inferred), encoding="utf-8")
            metadata = self.semantics([selector("20", "LINE")], holes=0, topology="open-strokes")
            with self.subTest(gap=gap), self.assertRaises(ValueError):
                adapter.import_shape(self.source, tolerance_mm=.01, connection_tolerance_mm=.00001,
                                     material_semantics=metadata)

    def test_coincident_engraving_does_not_remove_a_different_handles_material_edge(self):
        points = [(20, 20), (40, 20), (40, 40), (20, 40)]
        edges = "".join(line(f"{0x30 + i:X}", a, b)
                        for i, (a, b) in enumerate(zip(points, points[1:] + points[:1])))
        self.source.write_text(drawing(rectangle("10", 0, 0, 100, 100) + edges +
                                       line("20", points[0], points[1])), encoding="utf-8")
        result = self.run_import(self.semantics([selector("20", "LINE")], topology="open-strokes"))
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        geometry = result["geometry"]
        self.assertAlmostEqual(ring_area(geometry["outer"]) - sum(map(ring_area, geometry["holes"])),
                               9600, places=7)

    def test_legacy_polyline_role_covers_its_whole_vertex_family(self):
        self.source.write_text(drawing(rectangle("10", 0, 0, 100, 100) +
                                       rectangle("20", 20, 20, 20, 20, legacy=True) +
                                       circle("30", 70, 70, 5)), encoding="utf-8")
        result = self.run_import(self.semantics([selector("20", "POLYLINE")]))
        self.assertEqual(len(result["geometries"]), 1)
        self.assertEqual(len(result["geometry"]["holes"]), 1)
        with self.assertRaises(ValueError):
            self.run_import(self.semantics([selector("21", "VERTEX")]))

    def test_insert_instance_is_selected_by_handle_chain_not_block_name(self):
        contents = rectangle("10", 0, 0, 100, 100) + circle("20", 25, 25, 8) + circle("30", 70, 70, 5)
        self.source.write_text(drawing(insert("A", "plate") + insert("B", "plate", 120),
                                       block("plate", contents)), encoding="utf-8")
        metadata = self.semantics([selector("20", path=[step("A")])], components=2, holes=3)
        result = self.run_import(metadata)
        ordered = sorted(result["geometries"], key=lambda g: min(p[0] for p in g["outer"]))
        self.assertEqual([len(g["holes"]) for g in ordered], [1, 2])
        with self.assertRaises(ValueError):
            self.run_import(self.semantics([selector("20")], components=2, holes=2))

    def test_array_cell_is_part_of_the_expanded_entity_identity(self):
        contents = rectangle("10", 0, 0, 100, 100) + circle("20", 25, 25, 8) + circle("30", 70, 70, 5)
        self.source.write_text(drawing(insert("A", "plate", columns=2), block("plate", contents)), encoding="utf-8")
        result = self.run_import(self.semantics([selector("20", path=[step("A", column=1)])],
                                                components=2, holes=3))
        ordered = sorted(result["geometries"], key=lambda g: min(p[0] for p in g["outer"]))
        self.assertEqual([len(g["holes"]) for g in ordered], [2, 1])

    def test_nested_insert_identity_contains_every_parent_and_array_cell(self):
        contents = rectangle("10", 0, 0, 100, 100) + circle("20", 25, 25, 8) + circle("30", 70, 70, 5)
        blocks = block("plate", contents) + block("wrapper", insert("B", "plate", columns=2))
        self.source.write_text(drawing(insert("A", "wrapper"), blocks), encoding="utf-8")
        targets = [selector("20", path=[step("A"), step("B", column=1)])]
        result = self.run_import(self.semantics(targets, components=2, holes=3))
        ordered = sorted(result["geometries"], key=lambda g: min(p[0] for p in g["outer"]))
        self.assertEqual([len(g["holes"]) for g in ordered], [2, 1])
        with self.assertRaises(ValueError):
            self.run_import(self.semantics([selector("20", path=[step("B", column=1)])],
                                           components=2, holes=3))

    def test_duplicate_source_handle_is_ambiguous_even_at_top_level(self):
        self.source.write_text(drawing(rectangle("10", 0, 0, 100, 100) +
                                       circle("20", 25, 25, 8) + circle("20", 70, 70, 5)), encoding="utf-8")
        with self.assertRaises(ValueError):
            self.run_import(self.semantics())

    def test_duplicate_json_key_is_rejected_before_applying_any_role(self):
        metadata = self.semantics()
        encoded = json.dumps(metadata)
        encoded = encoded.replace('"schemaVersion": 1', '"schemaVersion": 1, "schemaVersion": 1', 1)
        sidecar = self.directory / "semantics.json"
        sidecar.write_text(encoded, encoding="utf-8")
        with self.assertRaises(ValueError):
            self.run_import(sidecar)


if __name__ == "__main__":
    unittest.main()
