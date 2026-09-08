"""Dimension metadata stays parametric, bounded and compatible with old packages."""
from __future__ import annotations

import copy
import importlib.util
import json
import math
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
SHARED = ROOT / "src/apps/tube-designer/templates/_shared"
SPEC = importlib.util.spec_from_file_location("profile_diagram_runtime", SHARED / "profile_package_runtime.py")
assert SPEC is not None and SPEC.loader is not None
RUNTIME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNTIME)


class ProfileParameterDiagramRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.packages = RUNTIME.generate({"action": "list-system"}, {})["systemProfiles"]
        self.rect = next(package for package in self.packages if package["descriptor"]["id"] == "rect")

    def evaluate(self, package, values=None, descriptor=None):
        return RUNTIME.generate({
            "action": "evaluate", "descriptor": descriptor or package["descriptor"],
            "scriptSource": package["scriptSource"], "values": values or {},
            "packageDigest": package["packageDigest"],
        }, {})["profile"]

    def annotation(self, profile, parameter):
        return next(item for item in profile["parameterDiagram"]["annotations"]
                    if item["parameter"] == parameter)

    def assert_valid_diagram(self, profile, descriptor):
        diagram = profile["parameterDiagram"]
        self.assertEqual(1, diagram["schemaVersion"])
        declared = {definition["key"] for definition in descriptor["parameters"]}
        self.assertEqual(declared, {item["parameter"] for item in diagram["annotations"]})
        for item in diagram["annotations"]:
            self.assertTrue(item["description"])
            fields = ("from", "to") if item["kind"] == "linear" else ("point",)
            for field in fields:
                self.assertEqual(2, len(item[field]))
                self.assertTrue(all(isinstance(value, (int, float)) and math.isfinite(value)
                                    for value in item[field]))
        json.dumps(diagram, ensure_ascii=False, allow_nan=False)

    def test_all_ten_builtins_cover_every_parameter_at_defaults_and_changed_sizes(self):
        self.assertEqual(10, len(self.packages))
        for package in self.packages:
            with self.subTest(profile=package["descriptor"]["id"]):
                self.assert_valid_diagram(package["previewProfile"], package["descriptor"])
                values = copy.deepcopy(package["defaultParameters"])
                for key in ("width", "depth", "wallThickness", "cornerRadius"):
                    if key in values:
                        values[key] *= 1.4
                result = self.evaluate(package, values)
                self.assert_valid_diagram(result, package["descriptor"])
                width = self.annotation(result, "width")
                self.assertAlmostEqual(values["width"], width["to"][0] - width["from"][0])
                self.assertNotEqual(package["previewProfile"]["parameterDiagram"], result["parameterDiagram"])
                if "depth" in values:
                    depth = self.annotation(result, "depth")
                    self.assertAlmostEqual(values["depth"], depth["to"][1] - depth["from"][1])

    def test_polygon_anchors_follow_real_odd_even_regular_and_star_geometry(self):
        package = next(item for item in self.packages if item["descriptor"]["id"] == "polygon")
        for mode in ("regular", "star"):
            for count in (3, 5, 6, 7, 8, 11):
                for ratio in (0.45, 0.7):
                    with self.subTest(mode=mode, count=count, ratio=ratio):
                        result = self.evaluate(package, {
                            "shapeMode": mode, "sideCount": count, "starInnerRatio": ratio,
                            "width": 97, "depth": 63, "wallThickness": 0.5,
                        })
                        points = result["contours"][0]["points"]
                        self.assertEqual(points[0], self.annotation(result, "shapeMode")["point"])
                        self.assertEqual(points[2], self.annotation(result, "sideCount")["point"])
                        self.assertEqual(points[1], self.annotation(result, "starInnerRatio")["point"])
                        self.assert_valid_diagram(result, package["descriptor"])

    def test_rectangle_corner_leader_lands_on_the_arc_not_its_missing_square_corner(self):
        result = self.evaluate(self.rect, {"width": 120, "depth": 80, "cornerRadius": 16})
        x, y = self.annotation(result, "cornerRadius")["point"]
        self.assertAlmostEqual(16, math.hypot(x - 44, y - 24))
        self.assertLess(x, 60)
        self.assertLess(y, 40)

    def test_metadata_free_package_is_still_supported_and_geometry_is_unchanged(self):
        for package in self.packages:
            descriptor = copy.deepcopy(package["descriptor"])
            descriptor.pop("parameterDiagram")
            old = self.evaluate(package, descriptor=descriptor)
            new = self.evaluate(package)
            self.assertNotIn("parameterDiagram", old)
            new.pop("parameterDiagram")
            self.assertEqual(old, new)

    def test_invalid_keys_shape_axis_and_expressions_are_rejected(self):
        invalid = [
            {"parameter": "notAParameter"}, {"parameter": []}, {"kind": "html"},
            {"side": "center"}, {"axis": "z"}, {"axis": "x", "side": "left"},
            {"from": [0]}, {"from": [True, 0]}, {"description": ""},
            {"from": ["__import__('os').getcwd()", 0]},
            {"from": ["width.__class__", 0]}, {"from": ["width[0]", 0]},
            {"from": ["[x for x in []]", 0]}, {"from": ["width**2", 0]},
            {"from": ["unknownParameter", 0]}, {"from": ["sum(width)", 0]},
            {"from": ["min()", 0]}, {"from": ["abs(width, 2)", 0]},
            {"from": ["max(width=2)", 0]}, {"from": ["1 + " * 90 + "1", 0]},
        ]
        for change in invalid:
            with self.subTest(change=change):
                descriptor = copy.deepcopy(self.rect["descriptor"])
                descriptor["parameterDiagram"]["annotations"][0].update(change)
                with self.assertRaisesRegex(ValueError, "parameterDiagram"):
                    self.evaluate(self.rect, descriptor=descriptor)

    def test_non_finite_division_and_invalid_contour_indices_are_rejected(self):
        expressions = [
            float("nan"), float("inf"), 10 ** 1000, "1e309", "1/0", "sqrt(-1)", "1e9*2",
            "contourX(0,0)", "contourX(-1,0)", "contourX(0,0.5)", "contourX(1000,0)",
        ]
        for expression in expressions:
            with self.subTest(expression=expression):
                descriptor = copy.deepcopy(self.rect["descriptor"])
                descriptor["parameterDiagram"]["annotations"][0]["from"] = [expression, 0]
                with self.assertRaisesRegex(ValueError, "parameterDiagram"):
                    self.evaluate(self.rect, descriptor=descriptor)
        polygon = next(item for item in self.packages if item["descriptor"]["id"] == "polygon")
        for expression in ("contourY(0,-1)", "contourY(0,999)", "contourY(1.5,0)"):
            descriptor = copy.deepcopy(polygon["descriptor"])
            descriptor["parameterDiagram"]["annotations"][0]["point"] = [expression, 0]
            with self.assertRaisesRegex(ValueError, "parameterDiagram"):
                self.evaluate(polygon, descriptor=descriptor)

    def test_non_numeric_parameters_cannot_be_used_as_coordinates(self):
        polygon = next(item for item in self.packages if item["descriptor"]["id"] == "polygon")
        descriptor = copy.deepcopy(polygon["descriptor"])
        descriptor["parameterDiagram"]["annotations"][0]["point"] = ["shapeMode", 0]
        with self.assertRaisesRegex(ValueError, "parameterDiagram"):
            self.evaluate(polygon, descriptor=descriptor)

    def test_annotation_schema_and_count_are_bounded(self):
        invalid = [{}, [], {"schemaVersion": 2, "annotations": []},
                   {"schemaVersion": True, "annotations": []},
                   {"schemaVersion": 1, "annotations": []},
                   {"schemaVersion": 1, "annotations": [{}] * 129}]
        for diagram in invalid:
            descriptor = copy.deepcopy(self.rect["descriptor"])
            descriptor["parameterDiagram"] = diagram
            with self.assertRaisesRegex(ValueError, "parameterDiagram"):
                self.evaluate(self.rect, descriptor=descriptor)


if __name__ == "__main__":
    unittest.main()
