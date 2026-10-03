"""Nominal target poses follow material position, independently of shop order."""
import copy
import importlib.util
import math
import unittest

from AssemblyProcessReuseTests import TEMPLATES, runtime, stock, instance, process


SPEC = importlib.util.spec_from_file_location("tested_fold_visualization", TEMPLATES / "_shared/fold_visualization.py")
visual = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(visual)


def endpoint(part):
    matrix, length = part["matrix"], part["request"]["length"]
    return [matrix[4*i+3]+matrix[4*i]*length for i in range(3)]


class FoldVisualizationTests(unittest.TestCase):
    def plan(self, instances):
        return runtime.resolve_process_plan([stock()], instances)

    def test_three_v_folds_produce_four_independent_labelled_material_targets(self):
        plan = self.plan([instance("first", 150), instance("second", 500, 90), instance("third", 850, 180)])
        before = copy.deepcopy(plan)
        parts = visual.build_target_parts(plan)
        self.assertEqual(before, plan)
        self.assertEqual(4, len(parts))
        self.assertEqual([[0, 150], [150, 500], [500, 850], [850, 1000]],
                         [part["materialInterval"] for part in parts])
        self.assertEqual(1000, sum(part["request"]["length"] for part in parts))
        self.assertEqual([1, 0, 0], [parts[0]["matrix"][i] for i in (0, 4, 8)])
        for part in parts:
            self.assertTrue(part["approximate"])
            self.assertEqual("target-shape", part["previewKind"])
            self.assertEqual("target-preview", part["formingValidation"])
            self.assertEqual([], part["request"]["features"])
        second_axis = [parts[1]["matrix"][i] for i in (0, 4, 8)]
        for actual, expected in zip(second_axis, [0, 0, 1]):
            self.assertAlmostEqual(expected, actual, places=10)

    def test_execution_sequence_and_input_order_do_not_change_target_geometry(self):
        instances = [instance("first", 150), instance("second", 500, 90), instance("third", 850, 180)]
        ordered = visual.build_target_parts(self.plan(instances))
        reversed_instances = list(reversed(copy.deepcopy(instances)))
        for index, operation in enumerate(reversed_instances):
            operation["processInput"]["geometry"]["sequence"] = index
        self.assertEqual(ordered, visual.build_target_parts(self.plan(reversed_instances)))

    def test_stock_world_pose_is_applied_once(self):
        base = stock()
        base["matrix"] = [0, -1, 0, 17, 1, 0, 0, 23, 0, 0, 1, 31, 0, 0, 0, 1]
        plan = runtime.resolve_process_plan([base], [instance("fold", 500)])
        parts = visual.build_target_parts(plan)
        self.assertEqual([17, 23, 31], [parts[0]["matrix"][i] for i in (3, 7, 11)])
        self.assertEqual([0, 1, 0], [parts[0]["matrix"][i] for i in (0, 4, 8)])
        self.assertEqual(2, len(parts))

    def test_segmented_bend_exposes_every_hinge_without_fake_l_product(self):
        operation = {"instanceId": "segmented", "templateId": "segmented-bend",
                     "targets": {"stock": "tube-1"}, "processInput": process(),
                     "parameters": {"bendRadius": 120}}
        plan = self.plan([operation])
        parts = visual.build_target_parts(plan)
        self.assertEqual(7, len(parts))
        self.assertEqual(1000, sum(part["request"]["length"] for part in parts))
        self.assertNotIn("shapeId", repr(parts))

    def test_cold_and_flexible_curves_use_labelled_chords_and_correct_end_pose(self):
        for template, parameters in (("bend", {"bendRadius": 120, "bendFactor": 0.5}),
                                     ("flexible-slit-bend-integrated", {"bendRadius": 120})):
            with self.subTest(template=template):
                operation = {"instanceId": "arc", "templateId": template,
                             "targets": {"stock": "tube-1"}, "processInput": process(rotation=90),
                             "parameters": parameters}
                plan = self.plan([operation])
                parts = visual.build_target_parts(plan)
                self.assertGreater(len(parts), 3)
                self.assertFalse(parts[0]["curvedApproximation"])
                self.assertFalse(parts[-1]["curvedApproximation"])
                curve = [part for part in parts if part["curvedApproximation"]]
                self.assertEqual(15, len(curve))
                for a, b in zip(parts, parts[1:]):
                    point = endpoint(a)
                    next_start = [b["matrix"][i] for i in (3, 7, 11)]
                    for actual, expected in zip(point, next_start):
                        self.assertAlmostEqual(expected, actual, places=8)
                direction = [parts[-1]["matrix"][i] for i in (0, 4, 8)]
                for actual, expected in zip(direction, [0, -1, 0]):
                    self.assertAlmostEqual(expected, actual, places=8)

    def test_sub_millimeter_chords_keep_native_length_valid_and_exact_target_endpoints(self):
        for template in ("bend", "flexible-slit-bend-integrated"):
            with self.subTest(template=template):
                operation = {"instanceId": "short-arc", "templateId": template,
                    "targets": {"stock": "tube-1"}, "processInput": process(angle=1),
                    "parameters": {"bendRadius": 180}}
                if template == "flexible-slit-bend-integrated":
                    operation["processDrafts"] = {"node-slot": {"flexible-slit-bend": {
                        "slitCount": 2, "slitWidth": 0.1, "minimumLand": 0.1}}}
                plan = self.plan([operation])
                before = copy.deepcopy(plan)
                parts = visual.build_target_parts(plan)
                self.assertEqual(before, plan)
                curve = [part for part in parts if part["curvedApproximation"]]
                self.assertEqual(8, len(curve))
                self.assertTrue(all(part["request"]["length"] >= 1 for part in parts))
                self.assertTrue(all(part["targetLength"] < 1 and part["displayAffine"] for part in curve))
                for part in curve:
                    matrix = part["matrix"]
                    axis_scale = math.sqrt(sum(matrix[i]*matrix[i] for i in (0, 4, 8)))
                    self.assertAlmostEqual(part["targetLength"], axis_scale*part["request"]["length"])
                    self.assertAlmostEqual(1, math.sqrt(sum(matrix[i]*matrix[i] for i in (1, 5, 9))))
                    self.assertAlmostEqual(1, math.sqrt(sum(matrix[i]*matrix[i] for i in (2, 6, 10))))
                for a, b in zip(parts, parts[1:]):
                    for actual, expected in zip(endpoint(a), [b["matrix"][i] for i in (3, 7, 11)]):
                        self.assertAlmostEqual(expected, actual, places=9)
                zone = plan["forming"][0]["calculation"].get("bendZoneLength") or (
                    plan["forming"][0]["calculation"]["flexibleLength"])
                expected = [500-zone/2+180*math.sin(math.radians(1)), 0,
                            180*(1-math.cos(math.radians(1)))]
                for actual, value in zip(endpoint(curve[-1]), expected):
                    self.assertAlmostEqual(value, actual, places=9)

    def test_short_straight_target_uses_display_scale_without_enlarging_material(self):
        plan = self.plan([instance("near-start", 0.5, angle=1)])
        before = copy.deepcopy(plan)
        parts = visual.build_target_parts(plan)
        self.assertEqual(before, plan)
        first = parts[0]
        self.assertEqual([0, 0.5], first["materialInterval"])
        self.assertEqual(0.5, first["targetLength"])
        self.assertEqual(1, first["request"]["length"])
        self.assertTrue(first["displayAffine"])
        self.assertEqual([0.5, 0, 0], endpoint(first))
        self.assertEqual(1000, sum(p["materialInterval"][1]-p["materialInterval"][0] for p in parts))


if __name__ == "__main__":
    unittest.main()
