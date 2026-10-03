"""Assembly preflight and template examples cover executable boundary inputs."""
import copy
import importlib.util
import json
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[5]
TEMPLATES = SOURCE / "apps/tube-designer/templates"
FIXED_PLANE = ("flexible-slit-bend-integrated", "segmented-bend", "node-v-notch-integrated",
               "node-edge-arc-integrated", "node-embedded-arc-integrated", "wrap-a-over-b")


class AssemblyTemplateBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runpy.run_path(str(TEMPLATES / "_shared/assembly_template_runtime.py"))

    def example(self, template, parameters=None):
        return self.runtime["get_example_product"](template, parameters)["finishedProduct"]

    def check(self, template, product, parameters=None):
        return self.runtime["check_applicability"](template, product, parameters)

    def plan(self, template, product, parameters=None):
        return self.runtime["preview_plan"](template, parameters, finished_product=product)

    def test_fixed_plane_contract_rejects_unsupported_rotation_and_preserves_input(self):
        for template in FIXED_PLANE:
            product = self.example(template)
            for rotation in (-180, -90, 0.1, 90, 180):
                product["parameters"]["planeRotation"] = rotation
                before = copy.deepcopy(product)
                with self.subTest(template=template, rotation=rotation):
                    result = self.check(template, product)
                    self.assertFalse(result["applicable"])
                    self.assertIn("planeRotation", result["reason"])
                    self.assertEqual(product, before)
            product["parameters"]["planeRotation"] = 0
            self.assertTrue(self.check(template, product)["applicable"])
        product = self.example("two-end-end-angle")
        product["parameters"]["planeRotation"] = 90
        self.assertTrue(self.check("two-end-end-angle", product)["applicable"])

    def test_bend_examples_at_supported_radius_boundaries_make_valid_plans(self):
        profiles = json.loads((TEMPLATES / "profile/rect/profile.json").read_text(encoding="utf-8"))
        minimums = {p["key"]: p["min"] for p in profiles["parameters"] if "min" in p}
        for template, radii in (("bend", (0.01, 40, 100000)),
                                ("flexible-slit-bend-integrated", (5, 40, 100000)),
                                ("segmented-bend", (1.5, 40, 100000))):
            for radius in radii:
                with self.subTest(template=template, radius=radius):
                    parameters = {"bendRadius": radius}
                    before = copy.deepcopy(parameters)
                    product = self.example(template, parameters)
                    self.assertEqual(parameters, before)
                    self.assertTrue(self.check(template, product, parameters)["applicable"])
                    self.assertGreaterEqual(product["parameters"]["angle"], 1)
                    for span in product["spans"].values():
                        for key, value in span["parameters"].items():
                            if key in minimums:
                                self.assertGreaterEqual(value, minimums[key])
                    plan = self.plan(template, product, parameters)
                    self.assertEqual(plan["finishedProduct"], product)
                    self.assertEqual(plan["parameters"]["bendRadius"], radius)
                    self.assertNotEqual(plan["resolvedWorkflow"]["validationStatus"], "failed")
                    self.assertTrue(all(part["request"]["length"] <= 100000
                                        for part in plan["manufacturingParts"]))

    def test_old_unsupported_radius_drafts_fail_explicitly(self):
        for template, radius in (("flexible-slit-bend-integrated", 0.01),
                                 ("flexible-slit-bend-integrated", 0.51),
                                 ("segmented-bend", 0.01)):
            with self.subTest(template=template, radius=radius):
                with self.assertRaisesRegex(ValueError, "bendRadius"):
                    self.example(template, {"bendRadius": radius})
                check = self.check(template, self.example(template), {"bendRadius": radius})
                self.assertFalse(check["applicable"])
                self.assertIn("bendRadius", check["reason"])

    def test_preflight_rejects_slot_land_root_and_stock_failures_before_plan_generation(self):
        for template, radius, angle in (("segmented-bend", 1.5, 90),
                                        ("segmented-bend", 100000, 90),
                                        ("flexible-slit-bend-integrated", 5, 90),
                                        ("flexible-slit-bend-integrated", 40, 1)):
            product = self.runtime["finished_products"].create("l")
            product["parameters"]["angle"] = angle
            with self.subTest(template=template, radius=radius, angle=angle):
                result = self.check(template, product, {"bendRadius": radius})
                self.assertFalse(result["applicable"])
                self.assertTrue(result["reason"])
        product = self.example("segmented-bend", {"bendRadius": 1.5})
        for span in product["spans"].values():
            span["parameters"]["depth"] = 1.05
        self.assertFalse(self.check("segmented-bend", product, {"bendRadius": 1.5})["applicable"])

    def test_t_insertion_stock_includes_required_extension(self):
        for template, key in (("end-side-tab-slot", "tabLength"), ("t-profile-insert", "insertDepth")):
            product = self.example(template)
            depth = 12
            with self.subTest(template=template):
                product["spans"]["branch"]["length"] = 100000 - depth
                self.assertTrue(self.check(template, product, {key: depth})["applicable"])
                plan = self.plan(template, product, {key: depth})
                self.assertEqual(max(part["request"]["length"] for part in plan["manufacturingParts"]), 100000)
                product["spans"]["branch"]["length"] += 0.001
                result = self.check(template, product, {key: depth})
                self.assertFalse(result["applicable"])
                self.assertIn("下料", result["reason"])

    def test_four_end_example_resizes_stock_for_declared_miter_extremes(self):
        for angle in (0, 45, 80):
            product = self.example("four-end-end-end-end", {"miterAngle": angle})
            self.assertTrue(self.check("four-end-end-end-end", product, {"miterAngle": angle})["applicable"])
            self.plan("four-end-end-end-end", product, {"miterAngle": angle})

    def test_round_t_insertion_preserves_continuous_host_wall_bands(self):
        product = self.example("t-profile-insert")
        for span in product["spans"].values():
            span["profileRef"] = {"scope": "system", "id": "round"}
            span["parameters"] = {"width": 40, "wallThickness": 2,
                                   "innerOffsetX": 0, "innerOffsetY": 0}
        before = copy.deepcopy(product)
        rejected = self.check("t-profile-insert", product)
        self.assertFalse(rejected["applicable"])
        self.assertIn("连续壁厚", rejected["reason"])
        self.assertEqual(product, before)
        for branch_diameter in (20, 30):
            product["spans"]["branch"]["parameters"]["width"] = branch_diameter
            self.assertTrue(self.check("t-profile-insert", product)["applicable"])
            self.plan("t-profile-insert", product)
        # Clearance can consume the side bands even with a smaller branch;
        # an acute-axis opening can also run into a short stock end.
        self.assertFalse(self.check("t-profile-insert", product, {"fitGap": 10})["applicable"])
        product["parameters"]["intersectionAngle"] = 10
        product["spans"]["main"]["length"] = 100
        self.assertFalse(self.check("t-profile-insert", product)["applicable"])

    def test_default_slot_measurements_match_the_actual_cutting_model(self):
        layout = runpy.run_path(str(TEMPLATES / "assembly/segmented-bend/layout.py"))
        generate = runpy.run_path(str(TEMPLATES / "mold/segmented-bend/tool.py"))["generate"]
        defaults = layout["_defaults"]
        for depth, wall, radius, angle in ((40, 2, 40, 90), (1.11, 0.1, 1.5, 170),
                                           (40, 2, 100000, 1)):
            analysis = {"outside": {"min": [-30, -depth / 2], "max": [30, depth / 2]},
                        "inside": {"min": [-30 + wall, -depth / 2 + wall]}, "wallThickness": wall}
            tool = generate({**defaults, "bendRadius": radius, "angle": angle},
                            {"analysis": analysis})["calculation"]
            measured = layout["calculate"](depth, wall, radius, angle)
            self.assertAlmostEqual(measured["remainingLand"], tool["remainingLand"])
            self.assertAlmostEqual(measured["patternLength"], tool["patternLength"])

    def test_preflight_and_examples_do_not_load_generation_entries(self):
        original = importlib.util.spec_from_file_location
        def read_only_module(name, location, *args, **kwargs):
            if Path(location).name in ("tool.py", "assembly.py"):
                self.fail("适用性及示例数据不得加载几何生成入口")
            return original(name, location, *args, **kwargs)
        with patch("importlib.util.spec_from_file_location", side_effect=read_only_module):
            for template in FIXED_PLANE + ("bend", "four-end-end-end-end", "end-side-tab-slot", "t-profile-insert"):
                product = self.example(template)
                self.assertTrue(self.check(template, product)["applicable"])


if __name__ == "__main__":
    unittest.main()
