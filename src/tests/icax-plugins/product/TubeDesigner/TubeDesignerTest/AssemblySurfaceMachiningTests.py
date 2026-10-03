"""Local machining reuse, repeat invocation, and product boundary regressions."""
import ast
from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest

from WindowCatalogueTests import SRC, package
from icax_template_sdk import NeutralModel


SHARED = SRC / "apps/tube-designer/templates/_shared"


def load(name):
    spec = importlib.util.spec_from_file_location("surface-test-" + name, SHARED / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def request(stock, geometry):
    return {"schema": "icax.assembly-process-input", "schemaVersion": 1,
            "parts": {"stock": stock}, "geometry": geometry}


def frame(origin=None, x=None, y=None, inward=None):
    return {"origin": origin or [0, 0, 0], "xAxis": x or [1, 0, 0],
            "yAxis": y or [0, 0, 1], "depthAxis": inward or [0, 1, 0]}


class AssemblySurfaceMachiningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load("assembly_geometry_process_runtime")
        cls.functions = load("assembly_surface_machining")

    def surface(self, primitive=None, protected=None, inward=None):
        return request({"thickness": 20}, {
            "frame": frame([0, -10, 0] if inward is None else [0, 10, 0], inward=inward),
            "primitive": primitive or {"kind": "line", "a": [10, 20], "b": [90, 20]},
            "boundary": [0, 100, 0, 100], "protectedRegions": protected or []})

    def values(self, **changes):
        return dict({"depth": 4, "through": False, "grooveWidth": 6,
                     "tool": "flat", "vAngle": 0}, **changes)

    def test_function_is_local_and_inputs_parameters_are_immutable(self):
        for primitive in ({"kind": "line", "a": [10, 20], "b": [90, 20]},
                          {"kind": "ring", "center": [50, 50], "radius": 20},
                          {"kind": "polygon", "points": [[10, 10], [30, 10], [30, 30], [10, 30]]}):
            local, values = self.surface(primitive), self.values()
            before = deepcopy((local, values))
            result = self.runtime.evaluate("surface-feature-cut", local, values)
            self.assertTrue(result["applicable"], result["reason"])
            self.assertEqual((local, values), before)
            self.assertEqual(result["parameters"], values)
            self.assertEqual(result["operations"][0]["operation"], "subtract")
            self.assertEqual(result["operations"][0]["role"], "stock")
            self.assertFalse(any(word in str(result["processInput"]) for word in ("leaf", "door", "product")))

    def test_surface_other_side_and_rotated_frame_use_actual_axes(self):
        front = self.runtime.evaluate("surface-feature-cut", self.surface(), self.values())
        back = self.runtime.evaluate("surface-feature-cut", self.surface(inward=[0, -1, 0]), self.values())
        def first_extrude(result):
            return next(node for node in result["geometry"] if node["operator"] == "extrude")
        self.assertEqual(first_extrude(front)["arguments"]["vector"], [0, 4.02, 0])
        self.assertEqual(first_extrude(back)["arguments"]["vector"], [0, -4.02, 0])
        rotated = self.surface()
        rotated["geometry"]["frame"] = frame([7, 11, 13], [0, 1, 0], [1, 0, 0], [0, 0, -1])
        result = self.runtime.evaluate("surface-feature-cut", rotated, self.values(tool="v", vAngle=90))
        self.assertTrue(result["applicable"], result["reason"])
        section = next(node for node in result["geometry"] if node["operator"] == "profile2d")
        self.assertEqual(section["arguments"]["placement"]["origin"], [27, 21, 13])
        self.assertEqual(section["arguments"]["placement"]["yAxis"], [0, 0, -1])
        self.assertEqual(first_extrude(result)["arguments"]["vector"], [0, 80, 0])

    def test_protected_material_is_removed_from_cutter(self):
        result = self.runtime.evaluate("surface-feature-cut", self.surface(protected=[[30, 60, 0, 100]]), self.values())
        self.assertTrue(result["applicable"], result["reason"])
        safe = next(node for node in result["geometry"] if node["key"] == "surface.safe")
        self.assertEqual(safe["arguments"]["operation"], "subtract")
        self.assertEqual(len(safe["arguments"]["tools"]), 1)
        through = self.runtime.evaluate("surface-feature-cut", self.surface(protected=[[30, 60, 0, 100]]), self.values(through=True, depth=20))
        self.assertTrue(through["applicable"], through["reason"])
        self.assertFalse(any(node["key"] == "surface.safe" for node in through["geometry"]))

    def test_invalid_input_returns_no_cutters_or_operations(self):
        cases = []
        wrong_frame = self.surface()
        wrong_frame["geometry"]["frame"]["depthAxis"] = [1, 0, 0]
        cases.append((wrong_frame, self.values()))
        cases.append((self.surface(), self.values(depth=21)))
        cases.append((self.surface(), self.values(through=True, depth=20, tool="v", vAngle=90)))
        for local, values in cases:
            result = self.runtime.evaluate("surface-feature-cut", local, values)
            self.assertFalse(result["applicable"])
            self.assertTrue(result["reason"])
            self.assertEqual(result["geometry"], [])
            self.assertEqual(result["operations"], [])

    def test_aperture_checks_actual_raw_bounds(self):
        local = request({"length": 100, "width": 30, "depth": 20}, {
            "frame": frame(), "station": 25, "across": 15})
        values = {"kind": "roundThroughDepth", "width": 8, "height": 8}
        result = self.runtime.evaluate("through-depth-aperture", local, values)
        self.assertTrue(result["applicable"], result["reason"])
        self.assertEqual(result["geometry"][0]["arguments"]["placement"]["origin"], [15, -11, 25])
        local["geometry"]["station"] = 4
        invalid = self.runtime.evaluate("through-depth-aperture", local, values)
        self.assertFalse(invalid["applicable"])
        self.assertIn("超出料件边界", invalid["reason"])

    def test_repeated_sites_same_stock_and_retry_are_preserved(self):
        model = NeutralModel(template_id="local-stock-test", template_version="1", package_digest="", parameters={})
        raw = model.geometry("actual-stock", "box", arguments={"size": [100, 20, 100]})
        first = self.runtime.invoke(model, raw, "surface-feature-cut", self.surface(), self.values(), "cut.1", "plate.1")
        second_input = self.surface({"kind": "line", "a": [10, 70], "b": [90, 70]})
        second = self.runtime.invoke(model, first, "surface-feature-cut", second_input, self.values(), "cut.2", "plate.1")
        count = len(model._document["geometry"])
        self.assertEqual(self.runtime.invoke(model, first, "surface-feature-cut", second_input, self.values(), "cut.2", "plate.1"), second)
        self.assertEqual(len(model._document["geometry"]), count)
        records = model.build()["extensions"][self.runtime.EXTENSION]["instances"]
        self.assertEqual([record["stockId"] for record in records], ["plate.1", "plate.1"])
        self.assertEqual(records[1]["targetGeometry"], records[0]["resultGeometry"])
        self.assertNotEqual(records[0]["resultGeometry"], records[1]["resultGeometry"])
        before = deepcopy(model._document)
        with self.assertRaisesRegex(ValueError, "同一工艺实例"):
            self.runtime.invoke(model, first, "surface-feature-cut", self.surface(), self.values(), "cut.2", "plate.1")
        self.assertEqual(model._document, before)
        with self.assertRaises(ValueError):
            self.runtime.invoke(model, second, "surface-feature-cut", second_input, self.values(depth=21), "bad-cut", "plate.1")
        self.assertEqual(model._document, before)

    def test_standard_host_identity_and_section_do_not_change_local_function(self):
        local = self.surface()
        original = self.runtime.evaluate("surface-feature-cut", local, self.values())
        local["parts"]["stock"].update({"id": "actual-fixed-stock",
            "profileRef": {"scope": "system", "id": "rect"},
            "parameters": {}, "length": 100,
            "matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
            "section": {"profile": {"contours": [self.functions._path(
                [[-10, -50], [10, -50], [10, 50], [-10, 50]])]}}})
        before = deepcopy(local)
        result = self.runtime.evaluate("surface-feature-cut", local, self.values())
        self.assertTrue(result["applicable"], result["reason"])
        self.assertEqual(result["geometry"], original["geometry"])
        self.assertEqual(result["operations"], original["operations"])
        self.assertEqual(local, before)
        local["parts"]["stock"]["productParameters"] = {"doorType": "single"}
        self.assertFalse(self.runtime.evaluate("surface-feature-cut", local, self.values())["applicable"])

    def test_same_local_prototype_keeps_each_stock_identity(self):
        model = NeutralModel(template_id="local-stock-test", template_version="1", package_digest="", parameters={})
        raw = model.geometry("actual-stock", "box", arguments={"size": [100, 20, 100]})
        first = self.runtime.invoke(model, raw, "surface-feature-cut", self.surface(), self.values(), "member.1.cut", "member.1")
        count = len(model._document["geometry"])
        second = self.runtime.invoke(model, raw, "surface-feature-cut", self.surface(), self.values(), "member.2.cut", "member.2")
        self.assertEqual(first, second)
        self.assertEqual(len(model._document["geometry"]), count)
        self.assertEqual([record["stockId"] for record in model.build()["extensions"][self.runtime.EXTENSION]["instances"]], ["member.1", "member.2"])

    def test_product_scripts_do_not_build_machining_boolean_or_cutters(self):
        for name in ("aluminium_window", "decorative_door"):
            source = SRC / "apps/tube-designer/templates/product" / name / "template.py"
            tree = ast.parse(source.read_text(encoding="utf-8"))
            for node in ast.walk(tree):
                if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and
                        node.func.attr == "geometry" and len(node.args) > 1 and isinstance(node.args[1], ast.Constant)):
                    self.assertNotIn(node.args[1].value, ("boolean", "extrude", "profile2d"), (name, node.lineno))

    def test_products_record_each_local_call_and_display_is_independent(self):
        descriptor, defaults, door = package("decorative_door")
        values = dict(defaults, doorType="single", pattern="lines", lineCount=3, machiningSide="both", protectHardware=False)
        result = door.generate(values, {"template": descriptor, "geometryPurpose": "manufacturing"})
        records = result["extensions"][self.runtime.EXTENSION]["instances"]
        self.assertEqual(len(records), 6)
        self.assertEqual({record["stockId"] for record in records}, {"leaf.1"})
        self.assertEqual(len({record["instanceId"] for record in records}), 6)
        self.assertEqual({record["templateId"] for record in records}, {"surface-feature-cut"})
        for previous, following in zip(records, records[1:]):
            self.assertEqual(following["targetGeometry"], previous["resultGeometry"])
        shown = door.generate(values, {"template": descriptor, "geometryPurpose": "display"})
        self.assertNotIn(self.runtime.EXTENSION, shown["extensions"])
        self.assertFalse(any(node["operator"] == "boolean" for node in shown["geometry"]))


if __name__ == "__main__":
    unittest.main()
