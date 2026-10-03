"""Weld ends use the outside corner envelope, not insertion clearances."""
import copy
import unittest

from AssemblyOrthogonalCornerTests import TEMPLATES, load, participants, section


class OrthogonalCornerWeldTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load(TEMPLATES / "_shared/assembly_template_runtime.py",
                           "corner_weld_test_runtime")

    def product(self, width=38, depth=38, radius=2, host_wall=1.2):
        product = self.runtime.get_example_product("orthogonal-corner")["finishedProduct"]
        for role in ("armA", "armB"):
            product["spans"][role]["parameters"].update(
                width=38, depth=38, wallThickness=host_wall,
                cornerRadius=radius, innerRadius=max(0, radius-host_wall))
        product["spans"]["armC"]["parameters"].update(
            width=width, depth=depth, wallThickness=1.2,
            cornerRadius=radius, innerRadius=max(0, radius-1.2))
        return product

    def test_same_size_sharp_and_rounded_welds_preserve_product_and_echo(self):
        for ab_joint in ("miter", "wrap"):
            for radius in (0, 2):
                with self.subTest(ab_joint=ab_joint, radius=radius):
                    product = self.product(radius=radius)
                    before = copy.deepcopy(product)
                    values = {"abJoint": ab_joint, "cJoint": "weld"}
                    result = self.runtime.check_applicability(
                        "orthogonal-corner", product, values)
                    self.assertTrue(result["applicable"], result["reason"])
                    plan = self.runtime.preview_plan(
                        "orthogonal-corner", values, finished_product=product,
                        manufacturing_only=True)
                    self.assertEqual(plan["templateVersion"],
                                     self.runtime._template_by_id("orthogonal-corner")["version"])
                    self.assertEqual(plan["finishedProduct"], before)
                    self.assertEqual(plan["parameters"],
                        self.runtime._validated_values(
                            self.runtime._template_by_id("orthogonal-corner"), values))
                    c_part = next(part for part in plan["manufacturingParts"]
                                  if part["sourceRole"] == "memberC")
                    self.assertEqual(c_part["request"]["ends"]["start"]["trim"], 19)
                    self.assertEqual(product, before)

    def test_both_c_directions_must_fit_outside_corner(self):
        for width, depth in ((38, 30), (30, 38), (37.9, 37.9)):
            with self.subTest(width=width, depth=depth):
                result = self.runtime.check_applicability(
                    "orthogonal-corner", self.product(width, depth), {"cJoint": "weld"})
                self.assertTrue(result["applicable"], result["reason"])
        for width, depth in ((38.001, 38), (38, 38.001), (40, 30), (30, 40)):
            with self.subTest(width=width, depth=depth):
                result = self.runtime.check_applicability(
                    "orthogonal-corner", self.product(width, depth), {"cJoint": "weld"})
                self.assertFalse(result["applicable"])
                self.assertIn("外包络", result["reason"])

    def test_weld_does_not_need_c_to_fit_host_cavity(self):
        product = self.product(radius=0, host_wall=10)
        weld = self.runtime.check_applicability(
            "orthogonal-corner", product, {"cJoint": "weld"})
        self.assertTrue(weld["applicable"], weld["reason"])
        for joint in ("insert", "tabs"):
            with self.subTest(joint=joint):
                result = self.runtime.check_applicability(
                    "orthogonal-corner", product, {"cJoint": joint})
                self.assertFalse(result["applicable"])
                self.assertIn("内孔", result["reason"])

    def test_equal_size_insert_and_tabs_still_rejected(self):
        for radius in (0, 2):
            for joint in ("insert", "tabs"):
                with self.subTest(radius=radius, joint=joint):
                    result = self.runtime.check_applicability(
                        "orthogonal-corner", self.product(radius=radius), {"cJoint": joint})
                    self.assertFalse(result["applicable"])
                    self.assertIn("内孔", result["reason"])

    def test_weld_still_rejects_offset_unequal_hosts_and_short_members(self):
        for role, field, value in (("armC", "innerOffsetX", 0.5),
                                   ("armB", "depth", 39)):
            product = self.product()
            product["spans"][role]["parameters"][field] = value
            with self.subTest(role=role, field=field):
                result = self.runtime.check_applicability(
                    "orthogonal-corner", product, {"cJoint": "weld"})
                self.assertFalse(result["applicable"])
        for role in ("armA", "armB", "armC"):
            product = self.product()
            product["spans"][role]["length"] = 10
            with self.subTest(role=role):
                result = self.runtime.check_applicability(
                    "orthogonal-corner", product, {"cJoint": "weld"})
                self.assertFalse(result["applicable"])

    def test_bound_actual_equal_size_weld_keeps_sources(self):
        actual = participants()
        for part in actual:
            old = part["section"]["sectionFrame"]
            part["section"] = section(38, 38, 1.2,
                old["xAxisWorld"], old["yAxisWorld"])
            if part["role"] != "memberC":
                part["length"] = 278
                part["anchor"]["stockAllowance"] = 19
        before = copy.deepcopy(actual)
        for ab_joint in ("miter", "wrap"):
            with self.subTest(ab_joint=ab_joint):
                plan = self.runtime.resolve_bound_plan("orthogonal-corner",
                    {"abJoint": ab_joint, "cJoint": "weld"},
                    participants=actual, product_topology="orthogonal-corner")
                self.assertTrue(plan["supported"])
                c_part = next(part for part in plan["parts"]
                              if part["role"] == "memberC")
                self.assertEqual(c_part["ends"]["start"]["trim"], 19)
                self.assertEqual(len(plan["nodeContacts"]), 3 if ab_joint == "miter" else 2)
                self.assertEqual(actual, before)


if __name__ == "__main__":
    unittest.main()
