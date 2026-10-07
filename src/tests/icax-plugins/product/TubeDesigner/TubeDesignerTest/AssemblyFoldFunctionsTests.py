"""Reusable folding consumes material geometry, independent of product shapes."""
import copy
import importlib.util
import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[6]
TEMPLATES = ROOT / "src/apps/tube-designer/templates"
SPEC = importlib.util.spec_from_file_location(
    "tested_assembly_fold_functions", TEMPLATES / "_shared/assembly_fold_functions.py")
folds = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(folds)


def stock(length=1000, width=60, depth=40):
    return {"profileRef": {"scope": "system", "id": "rect"},
            "parameters": {"width": width, "depth": depth, "wallThickness": 2,
                           "cornerRadius": 3, "innerRadius": 1}, "length": length}


def process(station=500, rotation=0, angle=90, part=None):
    radians = math.radians(rotation)
    c, s = math.cos(radians), math.sin(radians)
    return {"schema": "icax.assembly-process-input", "schemaVersion": 1,
            "parts": {"stock": part or stock()}, "geometry": {
                "angle": angle, "station": station, "frame": {
                    "origin": [station, 0, 0], "xAxis": [1, 0, 0],
                    "yAxis": [0, c, s], "zAxis": [0, -s, c]}}}


def values(template):
    if template == "bend":
        return {"bendRadius": 120, "bendFactor": 0.5}
    if template in ("segmented-bend", "flexible-slit-bend-integrated"):
        return {"bendRadius": 120}
    return {}


class FoldFunctionsTests(unittest.TestCase):
    def evaluate(self, template, data=None, parameters=None, drafts=None, resolved=None):
        result = folds.evaluate_fold(template, data or process(),
                                     values(template) if parameters is None else parameters,
                                     drafts, resolved)
        self.assertTrue(result["applicable"], result["reason"])
        return result

    def test_all_six_are_material_functions_and_echo_immutable_input(self):
        for template in sorted(folds.FOLD_IDS):
            with self.subTest(template=template):
                data, parameters = process(), values(template)
                before = copy.deepcopy((data, parameters))
                result = self.evaluate(template, data, parameters)
                self.assertEqual(before, (data, parameters))
                self.assertEqual(parameters, result["parameters"])
                self.assertEqual(1, len(result["materialRequirements"]))
                self.assertEqual(1, len(result["forming"]))
                self.assertEqual("stock", result["forming"][0]["role"])
                self.assertEqual(0, result["materialRequirements"][0]["lengthAddition"])
                self.assertEqual(0 if template == "bend" else 1, len(result["operations"]))
                self.assertNotIn("shapeId", repr(result))
                self.assertNotIn("armA", repr(result))
                for operation in result["operations"]:
                    self.assertNotIn("model", operation["cutter"])
                    self.assertEqual("start", operation["requestFeature"]["reference"])
                    self.assertEqual(500, operation["requestFeature"]["station"])

    def test_repeated_v_instances_have_independent_station_and_faces(self):
        results = [self.evaluate("node-v-notch-integrated", process(station, rotation))
                   for station, rotation in ((150, 0), (500, 90), (850, 180))]
        features = [r["operations"][0]["requestFeature"] for r in results]
        self.assertEqual([150, 500, 850], [f["station"] for f in features])
        self.assertEqual([0, 90, 180], [round(f["rotation"]) for f in features])
        # A 90 degree material-frame turn makes this non-square stock 60 mm
        # deep for the cutter, so the slot envelope must change accordingly.
        widths = [r["operations"][0]["interval"][1]-r["operations"][0]["interval"][0]
                  for r in results]
        self.assertGreater(widths[1], widths[0]+30)
        self.assertAlmostEqual(widths[0], widths[2])
        for result, feature in zip(results, features):
            matrix = result["forming"][0]["targetTransform"]
            direction = [matrix[0], matrix[4], matrix[8]]
            expected = result["forming"][0]["frame"]["zAxis"]
            for actual, target in zip(direction, expected):
                self.assertAlmostEqual(target, actual, places=10)
            self.assertEqual(1, len(result["operations"]))
            self.assertEqual(1000, result["materialRequirements"][0]["stockLength"])

    def test_signed_fold_changes_cut_wall_and_target_direction_together(self):
        positive = self.evaluate("node-v-notch-integrated", process(angle=70))
        negative = self.evaluate("node-v-notch-integrated", process(angle=-70))
        self.assertAlmostEqual(180, abs(negative["operations"][0]["requestFeature"]["rotation"]))
        self.assertEqual(positive["operations"][0]["toolParameters"],
                         negative["operations"][0]["toolParameters"])
        for result, sign in ((positive, 1), (negative, -1)):
            target = result["forming"][0]["targetTransform"]
            self.assertAlmostEqual(math.cos(math.radians(70)), target[0])
            self.assertAlmostEqual(sign*math.sin(math.radians(70)), target[8])

    def test_tool_outputs_reuse_geometry_without_station_in_body(self):
        template = "node-v-notch-integrated"
        data_a, data_b = process(200), process(800)
        outputs = [self.evaluate(template, data) for data in (data_a, data_b)]
        self.assertEqual(outputs[0]["operations"][0]["toolParameters"],
                         outputs[1]["operations"][0]["toolParameters"])
        geometries = []
        for data, result in zip((data_a, data_b), outputs):
            profile, section = folds._section(data["parts"]["stock"])
            operation = result["operations"][0]
            snapshot = folds._punch._evaluate(operation["toolRef"], operation["toolParameters"], {
                "target": "part", "lengthUnit": "mm", "targetSection": profile,
                "targetSectionAnalysis": section,
                "bounds": {"min": [0, -30, -20], "max": [1000, 30, 20]},
                "placement": {"rotation": 0}, "feature": {
                    "reference": "start", "station": data["geometry"]["station"]}})
            geometries.append(snapshot["geometry"])
        self.assertEqual(geometries[0], geometries[1])

    def test_each_fold_family_changes_real_geometry_for_its_own_process_parameter(self):
        cases = (
            ("bend", "bendFactor", 0.8),
            ("segmented-bend", "segmentCount", 9),
            ("node-v-notch-integrated", "leaveBottom", 2),
            ("node-embedded-arc-integrated", "arcRadius", 12),
            ("node-edge-arc-integrated", "leftArc", False),
            ("flexible-slit-bend-integrated", "slitCount", 9),
        )
        for template, key, changed_value in cases:
            with self.subTest(template=template, parameter=key):
                data, parameters = process(), values(template)
                original = copy.deepcopy(data)
                baseline = self.evaluate(template, data, parameters)
                if template == "bend":
                    revised = {**parameters, key: changed_value}
                    changed = self.evaluate(template, data, revised)
                    self.assertGreater(changed["materialRequirements"][0]["bendZoneLength"],
                                       baseline["materialRequirements"][0]["bendZoneLength"])
                    self.assertNotEqual(changed["forming"][0]["targetTransform"],
                                        baseline["forming"][0]["targetTransform"])
                    self.assertEqual(revised, changed["parameters"])
                else:
                    tool = folds.TOOLS[template]
                    drafts = {"node-slot": {tool: {key: changed_value}}}
                    drafts_before = copy.deepcopy(drafts)
                    changed = self.evaluate(template, data, parameters, drafts)
                    self.assertEqual(drafts_before, drafts)
                    self.assertEqual(parameters, changed["parameters"])
                    self.assertEqual(changed_value, changed["operations"][0]["toolParameters"][key])
                    profile, section = folds._section(data["parts"]["stock"])
                    models = []
                    for result in (baseline, changed):
                        operation = result["operations"][0]
                        generated = folds._punch._evaluate(operation["toolRef"], operation["toolParameters"], {
                            "target": "part", "targetSection": profile, "targetSectionAnalysis": section,
                            "bounds": {"min": [0, -30, -20], "max": [1000, 30, 20]},
                            "placement": {"rotation": 0},
                            "feature": {"reference": "start", "station": 500}})
                        models.append(generated["geometry"]["model"])
                    self.assertNotEqual(models[0], models[1])
                    if template == "segmented-bend":
                        self.assertEqual(9, len(changed["forming"][0]["localFolds"]))
                    elif template == "flexible-slit-bend-integrated":
                        self.assertEqual(9, changed["forming"][0]["calculation"]["slitCount"])
                        self.assertEqual(baseline["materialRequirements"][0]["bendZoneLength"],
                                         changed["materialRequirements"][0]["bendZoneLength"])
                    elif template == "node-v-notch-integrated":
                        self.assertAlmostEqual(1, changed["forming"][0]["hingePoint"][2]
                                               - baseline["forming"][0]["hingePoint"][2])
                    elif template == "node-edge-arc-integrated":
                        before, after = baseline['forming'][0], changed['forming'][0]
                        for index in (0, 1, 2, 4, 5, 6, 8, 9, 10):
                            self.assertAlmostEqual(before['targetTransform'][index], after['targetTransform'][index])
                        self.assertAlmostEqual(before['materialCornerReserves']['incoming'],
                                               after['materialCornerReserves']['outgoing'])
                        self.assertAlmostEqual(before['materialCornerReserves']['outgoing'],
                                               after['materialCornerReserves']['incoming'])
                        self.assertNotEqual(before['targetTransform'], after['targetTransform'])
                    else:
                        # Retained arc details change the cut, not the target bend angle.
                        self.assertEqual(baseline["forming"][0]["targetTransform"],
                                         changed["forming"][0]["targetTransform"])
                self.assertEqual(original, data)

    def test_recognized_v_root_rounding_stays_cutter_relief(self):
        rounded = self.evaluate("node-v-notch-integrated", drafts={"node-slot": {
            "v-notch-sharp": {"bottomStrategy": "rounded", "roundRadius": 2}}})
        sharp = self.evaluate("node-v-notch-integrated")
        p = rounded["operations"][0]["toolParameters"]
        self.assertEqual("rounded", p["bottomStrategy"])
        self.assertEqual(2, p["roundRadius"])
        self.assertEqual(sharp["forming"][0]["hingePoint"], rounded["forming"][0]["hingePoint"])
        self.assertEqual(sharp["forming"][0]["targetTransform"], rounded["forming"][0]["targetTransform"])
        self.assertNotIn("developedBandLength", rounded["forming"][0]["calculation"])
        self.assertEqual(0, rounded["materialRequirements"][0]["lengthAddition"])

    def test_explicit_exact_section_works_with_any_profile_identifier(self):
        part = stock()
        profile, section = folds._section(part)
        part.update(profileRef={"scope": "user", "id": "arbitrary-section"}, parameters={}, section=section)
        result = self.evaluate("node-v-notch-integrated", process(part=part))
        self.assertEqual(2, result["operations"][0]["toolParameters"]["wallThickness"])
        self.assertEqual(90, result["forming"][0]["angle"])

    def test_rejects_frames_that_native_cannot_execute_and_out_of_stock_zones(self):
        cases = []
        skew = process()
        skew["geometry"]["frame"].update(xAxis=[0, 1, 0], yAxis=[-1, 0, 0])
        cases.append(skew)
        mismatch = process()
        mismatch["geometry"]["frame"]["origin"][0] += 1
        cases.append(mismatch)
        reflection = process()
        reflection["geometry"]["frame"]["zAxis"] = [0, 0, -1]
        cases.append(reflection)
        scale = process()
        scale["geometry"]["frame"]["xAxis"] = [2, 0, 0]
        cases.append(scale)
        cases.extend([process(2), process(angle=180), process(angle=0)])
        for data in cases:
            with self.subTest(data=data["geometry"]):
                before = copy.deepcopy(data)
                result = folds.evaluate_fold("node-v-notch-integrated", data, {})
                self.assertFalse(result["applicable"])
                self.assertEqual([], result["operations"])
                self.assertEqual(before, data)

    def test_rowmajor_matrix_matches_axis_frame(self):
        axes = process(500, 90)
        matrix = copy.deepcopy(axes)
        matrix["geometry"]["frame"] = folds._matrix(axes["geometry"]["frame"])
        self.assertEqual(self.evaluate("node-v-notch-integrated", axes),
                         self.evaluate("node-v-notch-integrated", matrix))

    def test_segmented_process_reports_each_local_fold_and_measured_pattern(self):
        result = self.evaluate("segmented-bend")
        fold = result["forming"][0]
        self.assertEqual(6, len(fold["localFolds"]))
        self.assertAlmostEqual(90, sum(f["angle"] for f in fold["localFolds"]))
        self.assertAlmostEqual(math.cos(math.pi/2), fold["targetTransform"][0], places=10)
        self.assertAlmostEqual(1, fold["targetTransform"][8], places=10)
        self.assertAlmostEqual(fold["calculation"]["patternLength"],
                               result["materialRequirements"][0]["patternLength"])

    def test_cold_and_flexible_process_require_allocated_bending_region(self):
        for template, expected in (("bend", 121*math.pi/2),
                                   ("flexible-slit-bend-integrated", 120*math.pi/2)):
            with self.subTest(template=template):
                result = self.evaluate(template)
                requirement = result["materialRequirements"][0]
                self.assertAlmostEqual(expected, requirement["bendZoneLength"])
                self.assertAlmostEqual(expected, requirement["interval"][1]-requirement["interval"][0])
                self.assertEqual(0, requirement["lengthAddition"])
                self.assertFalse(folds.evaluate_fold(template, process(5), values(template))["applicable"])

    def test_resolved_tool_parameters_are_used_without_modifying_host_drafts(self):
        tool = "v-notch-sharp"
        parameters = folds._punch._package(TEMPLATES / "mold" / tool)[1]
        parameters.update(bottomStrategy="rounded", roundRadius=4)
        resolved = [{"id": "node-slot", "tool": tool, "values": parameters}]
        before = copy.deepcopy(resolved)
        result = self.evaluate("node-v-notch-integrated", resolved=resolved)
        self.assertEqual(4, result["operations"][0]["toolParameters"]["roundRadius"])
        self.assertEqual(before, resolved)


    def test_asymmetric_cut_uses_actual_envelope_and_requires_matching_fold_angle(self):
        drafts = {"node-slot": {"v-notch-sharp": {
            "asymmetric": True, "leftAngle": 20, "rightAngle": 50}}}
        result = self.evaluate("node-v-notch-integrated", process(angle=70), drafts=drafts)
        operation = result["operations"][0]
        self.assertEqual(operation["interval"], result["materialRequirements"][0]["interval"])
        self.assertGreater(operation["interval"][1]-500, 500-operation["interval"][0])
        mismatch = folds.evaluate_fold("node-v-notch-integrated", process(angle=90), {}, drafts)
        self.assertFalse(mismatch["applicable"])
        self.assertIn("开角", mismatch["reason"])

    def test_arc_target_transform_preserves_allocated_material_end_datum(self):
        for template in ("bend", "flexible-slit-bend-integrated"):
            for rotation in (0, 90):
                with self.subTest(template=template, rotation=rotation):
                    result = self.evaluate(template, process(rotation=rotation))
                    requirement, forming = result["materialRequirements"][0], result["forming"][0]
                    zone = requirement["bendZoneLength"]
                    radius = (forming["calculation"].get("bendRadius")
                              or forming["calculation"]["targetCenterlineRadius"])
                    expected = folds._point(forming["matrix"], [-zone/2+radius, 0, radius])
                    mapped = folds._point(forming["targetTransform"], [requirement["interval"][1], 0, 0])
                    for a, b in zip(expected, mapped):
                        self.assertAlmostEqual(a, b, places=10)


if __name__ == "__main__":
    unittest.main()
