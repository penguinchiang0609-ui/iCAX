"""Public template API regressions for clear-opening inputs and design review."""

from __future__ import annotations

from copy import deepcopy
import json
import math
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
TEMPLATES = ROOT / "src/apps/tube-designer/templates/product"
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template

NAMES = ("single_face_security_window", "two_face_security_window",
         "three_face_security_window", "five_face_security_window")
REVIEW = "tubeDesigner.securityWindowReview"


def template_input(name, **overrides):
    package = TEMPLATES / name
    descriptor = json.loads((package / "template.json").read_text(encoding="utf-8"))
    parameters = {item["key"]: deepcopy(item["defaultValue"]) for item in descriptor["parameters"]}
    parameters.update(overrides)
    context = {"template": {"id": descriptor["id"], "version": descriptor["version"],
                             "packageDigest": "security-window-public-tests"}}
    module = _load_template(str(package / "template.py"), context["template"]["packageDigest"])
    return module, parameters, context


def build(name, purpose="display", **overrides):
    module, parameters, context = template_input(name, **overrides)
    return module.generate(parameters, {**context, "geometryPurpose": purpose})


def graph(document):
    return {node["key"]: node for node in document["geometry"]}


def side_origin(document, prefix, side):
    candidates = [node for node in document["geometry"]
                  if node["operator"] == "transform" and node["key"].startswith(prefix)
                  and (f".display.{side}.solid" in node["key"]
                       or f".{side}.0001.solid" in node["key"])]
    if len(candidates) != 1:
        raise AssertionError(f"Expected one {prefix} {side} stock, got {len(candidates)}")
    candidate = candidates[0]
    origin = candidate["arguments"]["placement"]["origin"]
    # Processed frames are authored locally, then placed onto the selected face.
    placement_node = next((node for node in document["geometry"]
                           if node["key"].endswith(".surface.display")
                           and candidate["key"] in node.get("inputs", [])), None)
    if placement_node:
        placement = placement_node["arguments"]["placement"]
        origin = [placement["origin"][i] + sum(origin[j] * placement[axis][i]
                  for j, axis in enumerate(("xAxis", "yAxis", "zAxis"))) for i in range(3)]
    return origin


def clear_frame_size(document, prefix, section_width):
    return (math.dist(side_origin(document, prefix, "left"), side_origin(document, prefix, "right")) - section_width,
            math.dist(side_origin(document, prefix, "bottom"), side_origin(document, prefix, "top")) - section_width)


class SecurityWindowRulesTests(unittest.TestCase):
    def test_real_defaults_produce_design_and_actual_fixed_clear_dimensions(self):
        for name in NAMES:
            with self.subTest(template=name):
                _, parameters, _ = template_input(name)
                self.assertTrue(parameters["accessDoorEnabled"])
                self.assertEqual("escape", parameters["doorUse"])
                self.assertEqual((800, 1000), (parameters["doorClearWidth"], parameters["doorClearHeight"]))
                self.assertNotIn("doorWidth", parameters)
                self.assertNotIn("doorHeight", parameters)
                document = build(name)
                review = document["extensions"][REVIEW]
                self.assertEqual((800, 1000), (review["designClearWidth"], review["designClearHeight"]))
                self.assertEqual((830, 1000), (review["fixedClearWidth"], review["fixedClearHeight"]))
                actual = clear_frame_size(document, "access_door.fixed_frame.", parameters["doorFrameWidth"])
                self.assertAlmostEqual(830, actual[0])
                self.assertAlmostEqual(1000, actual[1])
                self.assertFalse(review["complianceCertified"])

    def test_public_parameters_are_exact_and_never_gain_private_dimensions(self):
        for name in NAMES:
            with self.subTest(template=name):
                module, parameters, context = template_input(name)
                snapshot = deepcopy(parameters)
                for purpose in ("display", "manufacturing"):
                    document = module.generate(parameters, {**context, "geometryPurpose": purpose})
                    self.assertEqual(snapshot, parameters)
                    self.assertEqual(snapshot, document["parameters"])
                    self.assertIsNot(document["parameters"], parameters)
                    self.assertNotIn("doorWidth", document["parameters"])
                    self.assertNotIn("doorHeight", document["parameters"])
                    self.assertNotIn("doorSizeReference", document["parameters"])
                    self.assertEqual(snapshot["doorVerticalCount"], document["parameters"]["doorVerticalCount"])
                    self.assertEqual(6, document["extensions"][REVIEW]["leafVerticalCount"])

    def test_display_and_manufacturing_preserve_review_parts_and_inputs(self):
        for name in NAMES:
            with self.subTest(template=name):
                display, manufacturing = build(name), build(name, "manufacturing")
                self.assertEqual(display["parameters"], manufacturing["parameters"])
                self.assertEqual(display["extensions"][REVIEW], manufacturing["extensions"][REVIEW])
                self.assertEqual(display["diagnostics"], manufacturing["diagnostics"])
                self.assertEqual(display["tables"], manufacturing["tables"])
                self.assertEqual(display["relationships"], manufacturing["relationships"])
                strip_geometry = lambda document: [{key: value for key, value in item.items()
                                                    if key != "representations"}
                                                   for item in document["items"]]
                self.assertEqual(strip_geometry(display), strip_geometry(manufacturing))
                self.assertFalse(any(node["operator"] == "boolean" for node in display["geometry"]))
                self.assertTrue(any(node["operator"] == "boolean" for node in manufacturing["geometry"]))

    def test_disabled_opening_ignores_unused_tiny_and_nonfinite_clear_dimensions(self):
        for name in NAMES:
            for clear_width, clear_height in ((1.0, 1.0), (math.nan, math.nan)):
                with self.subTest(template=name, width=clear_width):
                    document = build(name, accessDoorEnabled=False, doorClearWidth=clear_width,
                                     doorClearHeight=clear_height)
                    review = document["extensions"][REVIEW]
                    self.assertFalse(review["openingEnabled"])
                    self.assertEqual((0, 0), (review["fixedClearWidth"], review["fixedClearHeight"]))
                    self.assertFalse(any(item["key"].startswith("access_door.") for item in document["items"]))
                    self.assertIn("SW_NO_OPENING", {row["code"] for row in document["diagnostics"]})

    def test_small_escape_is_rejected_but_explicit_maintenance_is_allowed(self):
        for name in NAMES:
            for dimensions in ((799, 1000), (800, 999)):
                with self.subTest(template=name, dimensions=dimensions), self.assertRaisesRegex(ValueError, "800.*1000"):
                    build(name, doorClearWidth=dimensions[0], doorClearHeight=dimensions[1])
            with self.subTest(template=name, use="maintenance"):
                document = build(name, doorUse="maintenance", doorClearWidth=300, doorClearHeight=300)
                review = document["extensions"][REVIEW]
                self.assertEqual((300, 300), (review["designClearWidth"], review["designClearHeight"]))
                self.assertFalse(review["complianceCertified"])
                codes = {row["code"] for row in document["diagnostics"]}
                self.assertTrue({"SW_MAINTENANCE_ONLY", "SW_SMALL_OPENING"}.issubset(codes))

    def test_five_face_caps_only_allow_explicit_maintenance_openings(self):
        name = "five_face_security_window"
        for face in ("top", "bottom"):
            with self.subTest(face=face, use="escape"), self.assertRaisesRegex(ValueError, "立面|顶底"):
                build(name, accessDoorFace=face)
            with self.subTest(face=face, use="maintenance"):
                document = build(name, accessDoorFace=face, doorUse="maintenance",
                                 doorClearWidth=300, doorClearHeight=300, doorVOffset=100)
                review = document["extensions"][REVIEW]
                self.assertEqual((300, 300), (review["designClearWidth"], review["designClearHeight"]))
                actual = clear_frame_size(document, "access_door.fixed_frame.", 25.0)
                self.assertAlmostEqual(330.0, actual[0])
                self.assertAlmostEqual(300.0, actual[1])
                self.assertFalse(review["complianceCertified"])

    def test_auto_leaf_grid_actual_edge_and_internal_clearances_do_not_exceed_limit(self):
        for name in NAMES:
            with self.subTest(template=name):
                document = build(name)
                parameters = document["parameters"]
                left = side_origin(document, "access_door.leaf.frame.", "left")
                right = side_origin(document, "access_door.leaf.frame.", "right")
                distance = math.dist(left, right)
                axis = [(right[i] - left[i]) / distance for i in range(3)]
                nodes = graph(document)
                positions = []
                for item in document["items"]:
                    if item["key"].startswith("access_door.leaf.vertical."):
                        origin = nodes[item["representations"]["result"]]["arguments"]["placement"]["origin"]
                        positions.append(sum((origin[i] - left[i]) * axis[i] for i in range(3)))
                positions.sort()
                self.assertEqual(document["extensions"][REVIEW]["leafVerticalCount"], len(positions))
                frame_half, bar_half = parameters["doorLeafFrameWidth"] / 2, parameters["doorVerticalWidth"] / 2
                gaps = [positions[0] - frame_half - bar_half, distance - frame_half - positions[-1] - bar_half]
                gaps += [b - a - 2 * bar_half for a, b in zip(positions, positions[1:])]
                self.assertTrue(all(gap >= 0 for gap in gaps))
                self.assertLessEqual(max(gaps), parameters["maximumVerticalClearGap"] + 1e-7)

    def test_material_is_manufacturing_data_and_review_never_claims_certification(self):
        for name in NAMES:
            with self.subTest(template=name):
                unspecified = build(name, materialGrade="unspecified", surfaceTreatment="unspecified")
                codes = {row["code"] for row in unspecified["diagnostics"]}
                self.assertTrue({"SW_SITE_REVIEW", "SW_MATERIAL_UNSPECIFIED", "SW_FINISH_UNSPECIFIED"}.issubset(codes))
                self.assertTrue(all("manufacturing.material" not in item["properties"] for item in unspecified["items"]))
                selected = build(name, "manufacturing", materialGrade="304", surfaceTreatment="passivated")
                self.assertFalse(selected["extensions"][REVIEW]["complianceCertified"])
                for item in selected["items"]:
                    self.assertEqual("304", item["properties"]["manufacturing.material"])
                    self.assertEqual("passivated", item["properties"]["manufacturing.surfaceTreatment"])
                    self.assertEqual("required", item["properties"]["manufacturing.installationVerification"])
                codes = {row["code"] for row in selected["diagnostics"]}
                self.assertIn("SW_SITE_REVIEW", codes)
                self.assertNotIn("SW_MATERIAL_UNSPECIFIED", codes)
                self.assertNotIn("SW_FINISH_UNSPECIFIED", codes)
                table = next(table for table in selected["tables"] if table["key"] == "security_window_review")
                self.assertTrue(table["rows"])
                warnings = [row for row in table["rows"] if row["key"].startswith("review.")]
                self.assertTrue(all(row["values"]["status"] == "待核验" for row in warnings))
                self.assertEqual({"review." + code for code in codes if code.startswith("SW_")},
                                 {row["key"] for row in warnings})
                self.assertTrue(any(row["key"] == "process.assembly" for row in table["rows"]))

    def test_profile_dimensions_change_actual_frame_without_changing_clear_target(self):
        for name in NAMES:
            with self.subTest(template=name):
                document = build(name, doorFrameWidth=32.0, doorLeafFrameDepth=24.0)
                review = document["extensions"][REVIEW]
                self.assertEqual((800, 1000), (review["designClearWidth"], review["designClearHeight"]))
                self.assertEqual((834, 1000), (review["fixedClearWidth"], review["fixedClearHeight"]))
                actual = clear_frame_size(document, "access_door.fixed_frame.", 32.0)
                self.assertAlmostEqual(834.0, actual[0])
                self.assertAlmostEqual(1000.0, actual[1])
                self.assertAlmostEqual(898.0, actual[0] + 2 * 32.0)
                self.assertEqual(32.0, document["parameters"]["doorFrameWidth"])
                self.assertEqual(24.0, document["parameters"]["doorLeafFrameDepth"])
                self.assertNotIn("doorWidth", document["parameters"])

    def test_descriptor_presets_are_explicit_and_selected_default_matches_real_values(self):
        for name in NAMES:
            descriptor = json.loads((TEMPLATES / name / "template.json").read_text(encoding="utf-8"))
            module, defaults, _ = template_input(name)
            with self.subTest(template=name):
                self.assertEqual(descriptor["version"], module.TEMPLATE_VERSION)
                self.assertEqual(2, descriptor["extensions"]["securityWindow"]["reviewVersion"])
                definitions = {p["key"]: p for p in descriptor["parameters"]}
                self.assertEqual(len(definitions), len(descriptor["parameters"]))
                groups = {g["key"] for g in descriptor["groups"]}
                self.assertTrue(all(p["group"] in groups for p in definitions.values()))
                for preset in descriptor["extensions"]["parameterPresets"]["presets"]:
                    values = preset["values"]
                    self.assertTrue({"frameProfileType", "horizontalProfileType", "verticalProfileType",
                                     "materialGrade", "surfaceTreatment"}.issubset(values))
                    for key, value in values.items():
                        self.assertIn(key, definitions)
                        if "choices" in definitions[key]:
                            self.assertIn(value, [choice["value"] for choice in definitions[key]["choices"]])
                        if preset["value"] == defaults["tubeSpecificationPreset"]:
                            self.assertEqual(defaults[key], value, f"{name}: {key}")
                self.assertEqual("304", defaults["materialGrade"])
                self.assertEqual("passivated", defaults["surfaceTreatment"])
                self.assertEqual("insert", defaults["mainHorizontalConnection"])
                self.assertEqual({"op": "eq", "parameter": "mainHorizontalConnection", "value": "insert"},
                                 definitions["horizontalBranchReserve"]["visibleWhen"])
                if name.startswith("single"):
                    self.assertEqual("four_sides", defaults["frameLayout"])
                    self.assertTrue(all(defaults[key] == "miter_45" for key in
                                        ("frameJoinType", "doorFrameJoinType", "doorLeafFrameJoinType")))
                else:
                    self.assertEqual(defaults["frameWidth"], defaults["frameDepth"])
                    self.assertEqual("post_butt", defaults["frameCornerJoin"])
                    self.assertTrue({"sideHorizontalCount", "sideVerticalCount", "sideMaximumVerticalClearGap"}.issubset(defaults))

    def test_review_distinguishes_open_boundaries_and_only_active_fabrication_processes(self):
        for name in NAMES:
            with self.subTest(template=name):
                document = build(name)
                review = document["extensions"][REVIEW]
                self.assertEqual(2, review["reviewVersion"])
                self.assertFalse(review["vGrooveTrialRequired"])
                self.assertEqual("insert", review["mainHorizontalConnection"])
                codes = {d["code"] for d in document["diagnostics"]}
                self.assertEqual(name.startswith(("two", "three")), "SW_OPEN_BOUNDARY" in codes)
                self.assertEqual(not name.startswith("single"), "SW_PROJECTION_REVIEW" in codes)
                self.assertNotIn("SW_V_GROOVE_TRIAL", codes)
                self.assertNotIn("SW_MATERIAL_UNSPECIFIED", codes)
                self.assertNotIn("SW_FINISH_UNSPECIFIED", codes)
                if not name.startswith("single"):
                    miter = build(name, frameCornerJoin="rail_miter")
                    self.assertTrue(miter["extensions"][REVIEW]["displayHasUncutMiterStock"])
                    self.assertEqual("rail_miter", miter["extensions"][REVIEW]["outerFrameConnection"])
        single = NAMES[0]
        hidden = build(single, accessDoorEnabled=False, frameLayout="left_right",
                       frameJoinType="v_groove_90:sharp_v", doorFrameJoinType="v_groove_90:sharp_v",
                       doorLeafFrameJoinType="v_groove_90:sharp_v")
        self.assertFalse(hidden["extensions"][REVIEW]["vGrooveTrialRequired"])
        self.assertFalse(hidden["extensions"][REVIEW]["displayHasUncutMiterStock"])
        self.assertIn("SW_OPEN_BOUNDARY", {d["code"] for d in hidden["diagnostics"]})
        for key in ("frameJoinType", "doorFrameJoinType", "doorLeafFrameJoinType"):
            with self.subTest(join=key):
                active = build(single, **{key: "v_groove_90:sharp_v"})
                self.assertTrue(active["extensions"][REVIEW]["vGrooveTrialRequired"])
                self.assertIn("SW_V_GROOVE_TRIAL", {d["code"] for d in active["diagnostics"]})

    def test_every_material_preset_builds_both_connections_and_corner_options(self):
        for name in NAMES:
            descriptor = json.loads((TEMPLATES / name / "template.json").read_text(encoding="utf-8"))
            for preset in descriptor["extensions"]["parameterPresets"]["presets"]:
                corners = (None,) if name.startswith("single") else ("post_butt", "rail_miter")
                for corner in corners:
                    for connection in ("insert", "weld"):
                        values = {**preset["values"], "tubeSpecificationPreset": preset["value"],
                                  "mainHorizontalConnection": connection}
                        if corner is not None:
                            values["frameCornerJoin"] = corner
                        for purpose in ("display", "manufacturing"):
                            with self.subTest(template=name, preset=preset["value"], corner=corner,
                                              connection=connection, purpose=purpose):
                                document = build(name, purpose, **values)
                                self.assertTrue(document["items"])
                                self.assertEqual(values["materialGrade"], document["items"][0]["properties"]["manufacturing.material"])
                                self.assertEqual(connection, document["extensions"][REVIEW]["mainHorizontalConnection"])
                                self.assertFalse(document["extensions"][REVIEW]["complianceCertified"])


if __name__ == "__main__":
    unittest.main()
