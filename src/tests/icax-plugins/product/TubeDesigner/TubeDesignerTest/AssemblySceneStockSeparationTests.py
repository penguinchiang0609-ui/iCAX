"""Assembly process changes may shape a joint, but must not resize its input stock."""

import copy
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps" / "tube-designer" / "templates" / "_shared" / "assembly_template_runtime.py"


def load_runtime():
    spec = importlib.util.spec_from_file_location("assembly_stock_separation_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AssemblySceneStockSeparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load_runtime()

    def test_process_changes_preserve_input_stock_across_connection_families(self):
        cases = (
            ("two-end-end-angle", {"fitGap": 2}),
            ("two-end-middle", {"fitGap": 1}),
            ("saddle-weld", {"fitGap": 2, "weldLeg": 6}),
            ("mechanical-fastener", {"fastenerType": "adjustableBolt", "adjustment": 18}),
            ("slot-bolt-adjustable", {"adjustment": 20}),
            ("tab-slot-lock", {"nominalWidth": 24, "straightDepth": 22}),
            ("wrap-a-over-b", {"maleFemale": True, "tabLength": 18}),
            ("insert-sleeve", {"insertDepth": 150}),
        )
        for template_id, process_changes in cases:
            with self.subTest(template=template_id):
                baseline = self.runtime.preview_plan(template_id)
                changed = self.runtime.preview_plan(template_id, process_changes)
                self.assertEqual(changed["sceneParts"], baseline["sceneParts"])
                self.assertEqual(
                    [(part["role"], part["matrix"], part["request"]["profileRef"],
                      part["request"]["parameters"], part["request"]["length"])
                     for part in changed["designParts"]],
                    [(part["role"], part["matrix"], part["request"]["profileRef"],
                      part["request"]["parameters"], part["request"]["length"])
                     for part in baseline["designParts"]],
                )

    def test_sleeve_clearance_must_match_actual_stock_instead_of_resizing_it(self):
        product = self.runtime.get_example_product("insert-sleeve")["finishedProduct"]
        before = copy.deepcopy(product)
        with self.assertRaisesRegex(ValueError, "实际单侧间隙"):
            self.runtime.preview_plan("insert-sleeve", {"fitClearance": 0.8},
                                      finished_product=product)
        self.assertEqual(product, before)

    def test_straight_finished_members_meet_end_to_end_before_process(self):
        for template_id in ("insert-sleeve", "tab-slot-lock"):
            with self.subTest(template=template_id):
                plan = self.runtime.preview_plan(template_id)
                self.assertEqual(len(plan["designParts"]), 2)
                intervals = sorted((part["matrix"][3],
                                    part["matrix"][3] + part["request"]["length"])
                                   for part in plan["designParts"])
                self.assertAlmostEqual(intervals[0][1], intervals[1][0],
                    msg="成品两根管只在端面相接，不提前出现套接或公母插入")

    def test_mating_allowance_changes_cuts_without_moving_product_parts(self):
        baseline = self.runtime.preview_plan("two-end-end-angle")
        gapped = self.runtime.preview_plan("two-end-end-angle", {"fitGap": 2})
        self.assertIn("fitGap", gapped["parameters"])
        self.assertNotIn("fitGap", gapped["sceneParameters"])
        self.assertEqual(baseline["sceneParts"], gapped["sceneParts"])
        self.assertEqual([part["matrix"] for part in baseline["designParts"]],
                         [part["matrix"] for part in gapped["designParts"]])
        self.assertNotEqual([part["request"] for part in baseline["manufacturingParts"]],
                            [part["request"] for part in gapped["manufacturingParts"]])

    def test_wrap_tabs_change_cuts_but_not_finished_member_placement(self):
        plain = self.runtime.preview_plan("wrap-a-over-b")
        tabbed = self.runtime.preview_plan("wrap-a-over-b",
                                           {"maleFemale": True, "tabLength": 18})
        self.assertEqual([part["matrix"] for part in plain["designParts"]],
                         [part["matrix"] for part in tabbed["designParts"]])
        self.assertEqual(plain["sceneParts"], tabbed["sceneParts"])
        self.assertEqual(tabbed["manufacturingParts"][1]["request"]["ends"]["end"]
                         ["toolParameters"]["tabLength"], 18)

    def test_saddle_axis_angle_is_scene_geometry_shared_with_end_cut(self):
        descriptor = self.runtime._template_by_id("saddle-weld")
        angle = next(item for item in descriptor["parameters"]
                     if item["key"] == "intersectionAngle")
        self.assertEqual(angle["scope"], "scene")
        baseline = self.runtime.preview_plan("saddle-weld")
        angled = self.runtime.preview_plan("saddle-weld",
                                           scene_parameters={"intersectionAngle": 120})
        self.assertNotIn("intersectionAngle", angled["parameters"])
        self.assertEqual(angled["sceneParameters"]["intersectionAngle"], 120)
        self.assertEqual(baseline["sceneParts"], angled["sceneParts"])
        self.assertNotEqual(baseline["designParts"][0]["matrix"],
                            angled["designParts"][0]["matrix"])
        cope = next(item for item in angled["resolvedWorkflow"]["partOperations"]
                    if item["processId"] == "cope-branch")
        self.assertEqual(cope["operationParameters"]["angle"], 120)

    def test_t_process_templates_share_finished_product_geometry(self):
        cases = (
            ("t-contact-fit", {"markContact": True}),
            ("end-side-tab-slot", {"pairCount": "four"}),
            ("t-profile-insert", {"insertDepth": 18, "fitGap": 0.5}),
        )
        product = self.runtime.get_example_product("end-side-tab-slot")["finishedProduct"]
        reference = self.runtime.preview_plan("t-contact-fit", finished_product=product)
        for template_id, process_changes in cases:
            with self.subTest(template=template_id):
                for plan in (self.runtime.preview_plan(template_id, finished_product=product),
                             self.runtime.preview_plan(template_id, process_changes,
                                                       finished_product=product)):
                    self.assertEqual(plan["sceneParts"], reference["sceneParts"])
                    self.assertEqual(len(plan["designParts"]), len(reference["designParts"]))
                    for actual, expected in zip(plan["designParts"],
                                                reference["designParts"]):
                        self.assertEqual(actual["role"], expected["role"])
                        self.assertEqual(actual["request"], expected["request"])
                        for actual_value, expected_value in zip(actual["matrix"],
                                                               expected["matrix"]):
                            self.assertAlmostEqual(actual_value, expected_value)


if __name__ == "__main__":
    unittest.main()
