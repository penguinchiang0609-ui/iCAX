"""Derived section dimensions and manufacturing placements keep inputs intact."""
import copy
import json
from pathlib import Path
import runpy
import unittest

SOURCE = Path(__file__).resolve().parents[5]
RUNTIME = SOURCE / "apps/tube-designer/templates/_shared/assembly_template_runtime.py"


class AssemblyRuntimeDimensionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runpy.run_path(str(RUNTIME))

    def test_round_dimensions_are_private_formula_values(self):
        part = {"role": "member", "request": {
            "length": 260, "profileRef": {"scope": "system", "id": "round"},
            "parameters": {"width": 40, "wallThickness": 2}}}
        before = copy.deepcopy(part)
        values = {"gap": 1}
        result = self.runtime["_design_expression_values"](values, [part])
        self.assertEqual(result["member_depth"], 40)
        self.assertEqual(part, before)
        self.assertEqual(values, {"gap": 1})
        part["request"]["profileRef"] = {"scope": "user", "id": "round"}
        self.assertNotIn("member_depth", self.runtime["_design_expression_values"](values, [part]))

    def test_round_product_plans_preserve_profile_and_process_parameters(self):
        templates = ("saddle-weld", "t-profile-insert", "two-end-middle", "wrap-a-over-b",
                     "through-bolt", "mechanical-fastener", "slot-bolt-adjustable", "weld-interface")
        for template in templates:
            with self.subTest(template=template):
                descriptor = json.loads((RUNTIME.parent.parent / "assembly" / template /
                                         "assembly.json").read_text(encoding="utf-8"))
                product = self.runtime["finished_products"].create(descriptor["exampleInput"]["shapeId"])
                for span in product["spans"].values():
                    span["profileRef"] = {"scope": "system", "id": "round"}
                    span["parameters"] = {"width": 40, "wallThickness": 2,
                                          "innerOffsetX": 0, "innerOffsetY": 0}
                if template == "t-profile-insert":
                    product["spans"]["branch"]["parameters"]["width"] = 30
                values = {definition["key"]: copy.deepcopy(definition["defaultValue"])
                          for definition in descriptor["parameters"]}
                if template == "wrap-a-over-b":
                    values["maleFemale"] = False
                before = copy.deepcopy((product, values))
                check = self.runtime["check_applicability"](template, product, values)
                self.assertTrue(check["applicable"], check["reason"])
                plan = self.runtime["preview_plan"](template, values, finished_product=product)
                self.assertEqual((product, values), before)
                self.assertEqual(plan["finishedProduct"], product)
                self.assertEqual(plan["parameters"], values)
                for part in plan["manufacturingParts"]:
                    self.assertNotIn("depth", part["request"]["parameters"])

    def test_miter_matrices_do_not_apply_registration_twice(self):
        product = self.runtime["finished_products"].create("l")
        plan = self.runtime["preview_plan"]("two-end-end-angle", finished_product=product)
        second = next(part for part in plan["manufacturingParts"] if part["sourceRole"] == "memberB")
        self.assertEqual(second["matrix"], second["explodedMatrix"])
        # 100 mm template separation minus one 20 mm long-point allowance.
        self.assertAlmostEqual(second["matrix"][11], 80)
        original = copy.deepcopy(second["explodedMatrix"])
        second["matrix"][3] += 17
        self.assertEqual(second["explodedMatrix"], original)

    def test_sleeve_only_declares_supported_locks(self):
        descriptor = json.loads((RUNTIME.parent.parent / "assembly/insert-sleeve/assembly.json")
                                .read_text(encoding="utf-8"))
        locks = next(parameter for parameter in descriptor["parameters"] if parameter["key"] == "lockMethod")
        self.assertEqual([option["value"] for option in locks["options"]], ["none", "weld"])
        for lock in ("none", "weld"):
            product = self.runtime["get_example_product"]("insert-sleeve", {"lockMethod": lock})
            plan = self.runtime["preview_plan"]("insert-sleeve", {"lockMethod": lock},
                                               finished_product=product["finishedProduct"])
            self.assertEqual(plan["parameters"]["lockMethod"], lock)
        for lock in ("bolt", "pin"):
            with self.assertRaises(ValueError):
                self.runtime["get_example_product"]("insert-sleeve", {"lockMethod": lock})

    def test_inactive_profile_drafts_do_not_block_continuous_stock(self):
        defaults = {p["key"]: copy.deepcopy(p["defaultValue"]) for p in json.loads(
            (RUNTIME.parent.parent / "profile/rect/profile.json").read_text(encoding="utf-8"))["parameters"]}
        product = self.runtime["finished_products"].create("l")
        for span in product["spans"].values():
            span["parameters"] = copy.deepcopy(defaults)
        product["spans"]["armB"]["parameters"].update(outerRadius1=4, innerRadius1=2)
        before = copy.deepcopy(product)
        for template in ("bend", "flexible-slit-bend-integrated", "segmented-bend",
                         "node-v-notch-integrated", "node-edge-arc-integrated",
                         "node-embedded-arc-integrated", "two-end-end-angle"):
            with self.subTest(template=template):
                check = self.runtime["check_applicability"](template, product)
                self.assertTrue(check["applicable"], check["reason"])
                plan = self.runtime["preview_plan"](template, finished_product=product)
                self.assertEqual(plan["finishedProduct"], before)
                self.assertEqual(product, before)
        product["spans"]["armB"]["parameters"]["useOuterRadii"] = True
        self.assertFalse(self.runtime["check_applicability"]("bend", product)["applicable"])
        first = {"profileRef": {"scope": "user", "id": "unknown"}, "parameters": {"x": 1}}
        second = copy.deepcopy(first)
        second["parameters"]["x"] = 2
        self.assertFalse(self.runtime["section_queries"].sections_match(first, second))


if __name__ == "__main__":
    unittest.main()
