"""Pure-Python dimensional, graph and catalogue contracts; no native DLL loads."""
from __future__ import annotations

from itertools import combinations
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template

DIRECTORY = ROOT / "src/apps/tube-designer/templates/modular_guardrail"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {parameter["key"]: parameter["defaultValue"] for parameter in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "modular-guardrail-tests")
SUBJECT = sys.modules[MODULE.generate.__module__]
PRESETS = DESCRIPTOR["extensions"]["catalog"]["presets"]


def values(**changes):
    return {**DEFAULTS, **changes}


def bounds(tube):
    half = [abs(tube.x_axis[i]) * tube.profile.width / 2
            + abs(tube.y_axis[i]) * tube.profile.depth / 2 for i in range(3)]
    return ([min(tube.start[i], tube.end[i]) - half[i] for i in range(3)],
            [max(tube.start[i], tube.end[i]) + half[i] for i in range(3)])


def overlap(a, b):
    amin, amax = bounds(a)
    bmin, bmax = bounds(b)
    return all(min(amax[i], bmax[i]) - max(amin[i], bmin[i]) > 1e-6 for i in range(3))


class ModularGuardrailTests(unittest.TestCase):
    def test_twenty_presets_are_one_real_template_and_only_declared_parameters(self):
        self.assertEqual(DESCRIPTOR["id"], "modular-guardrail")
        self.assertEqual(len(PRESETS), 20)
        self.assertEqual(len({preset["id"] for preset in PRESETS}), 20)
        for preset in PRESETS:
            self.assertTrue(set(preset["parameters"]).issubset(DEFAULTS), preset["id"])
            self.assertTrue(preset["displayName"]["zh-CN"])

    def test_every_preset_builds_complete_display_and_export_graph(self):
        for preset in PRESETS:
            with self.subTest(preset=preset["id"]):
                document = MODULE.generate(values(**preset["parameters"]), {})
                json.dumps(document, allow_nan=False)
                keys = {node["key"] for node in document["geometry"]}
                self.assertEqual(len(keys), len(document["geometry"]))
                for node in document["geometry"]:
                    self.assertTrue(set(node["inputs"]).issubset(keys))
                items = {item["key"] for item in document["items"]}
                self.assertEqual(len(items), len(document["items"]))
                for item in document["items"]:
                    self.assertTrue(set(item["representations"].values()).issubset(keys))
                    props = item["properties"]
                    if props["manufacturing.partKind"] == "tube":
                        self.assertGreater(props["length"], 0)
                        self.assertIn("schemaVersion", props["tubeDesigner.profile"])
                    else:
                        self.assertNotIn("tubeDesigner.profile", props)
                        self.assertGreater(props["manufacturing.plate"]["areaMm2"], 0)
                for output in document["outputs"]:
                    self.assertEqual(set(output["items"]), items)

    def test_two_and_three_rails_both_have_vertical_bars(self):
        for count in (2, 3):
            built = MODULE.build_layout(values(railCount=count))
            self.assertTrue(any(part.category == "guardrail.vertical_bar" for part in built.tubes))
            cross = [part for part in built.tubes if part.category == "guardrail.cross_rail"]
            self.assertEqual(len(cross), len(built.bays) * (count - 1))

    def test_all_rectangular_presets_have_no_overlapping_part_envelopes(self):
        for preset in PRESETS:
            if preset["id"].startswith("round-"):
                continue
            with self.subTest(preset=preset["id"]):
                built = MODULE.build_layout(values(**preset["parameters"]))
                for a, b in combinations(built.tubes, 2):
                    self.assertFalse(overlap(a, b), (a.key, b.key))

    def test_each_bay_has_equal_clear_gaps_and_no_post_bar_collision(self):
        for preset in PRESETS[:18]:
            built = MODULE.build_layout(values(**preset["parameters"]))
            for bay in built.bays:
                with self.subTest(preset=preset["id"], bay=bay.key):
                    self.assertLessEqual(max(bay.clear_gaps), DEFAULTS["maximumVerticalClearGap"] + 1e-6)
                    self.assertAlmostEqual(min(bay.clear_gaps), max(bay.clear_gaps))
                    bars = [part for part in built.tubes if part.group == bay.key and part.category == "guardrail.vertical_bar"]
                    self.assertAlmostEqual(sum(bay.clear_gaps) + sum(part.profile.width for part in bars), bay.clear)

    def test_shared_corner_is_one_post_double_corner_is_two_nonoverlapping_posts(self):
        shared = MODULE.build_layout(values(layout="left_l"))
        double = MODULE.build_layout(values(layout="left_l", cornerPostMode="double"))
        self.assertEqual(len(double.posts), len(shared.posts) + 1)
        a, b = double.posts[3:5]
        gap_x = max(0, abs(a.point[0] - b.point[0]) - (a.profile.width + b.profile.width) / 2)
        gap_y = max(0, abs(a.point[1] - b.point[1]) - (a.profile.depth + b.profile.depth) / 2)
        self.assertAlmostEqual(math.hypot(gap_x, gap_y), DEFAULTS["doublePostClearGap"])

    def test_dimension_modes_are_explicit(self):
        outside = MODULE.build_layout(values(dimensionMode="outside_to_outside"))
        center = MODULE.build_layout(values(dimensionMode="center_to_center"))
        self.assertAlmostEqual(outside.posts[-1].point[0] - outside.posts[0].point[0], 2960)
        self.assertAlmostEqual(center.posts[-1].point[0] - center.posts[0].point[0], 3000)

    def test_guard_height_is_top_surface_not_centerline(self):
        built = MODULE.build_layout(values())
        self.assertAlmostEqual(max(bounds(part)[1][2] for part in built.tubes), DEFAULTS["guardHeight"])
        bottom = next(part for part in built.tubes if part.key.endswith(".bottom"))
        self.assertAlmostEqual(bounds(bottom)[0][2], DEFAULTS["bottomClearance"])

    def test_large_posts_split_caps_without_intersection(self):
        for mode in ("middle", "ends"):
            built = MODULE.build_layout(values(layout="u", largePostMode=mode))
            self.assertTrue(any(post.large for post in built.posts))
            for a, b in combinations(built.tubes, 2):
                self.assertFalse(overlap(a, b), (mode, a.key, b.key))

    def test_non_square_profiles_use_actual_axis_dimensions(self):
        built = MODULE.build_layout(values(layout="u", postWidth=60, postDepth=40,
                                            infillWidth=24, infillDepth=16, railWidth=25, railDepth=35))
        for bay in built.bays:
            self.assertLessEqual(max(bay.clear_gaps), 105 + 1e-6)
        for a, b in combinations(built.tubes, 2):
            self.assertFalse(overlap(a, b), (a.key, b.key))

    def test_fixed_center_keeps_symmetric_edges_and_max_gap(self):
        for clear in (60, 106, 230, 444, 1180, 5000):
            centers, gaps = SUBJECT.distribute_bars(clear, 20, 105, "fixed_center")
            self.assertAlmostEqual(gaps[0], gaps[-1])
            self.assertLessEqual(max(gaps), 105 + 1e-6)
            self.assertGreaterEqual(min(gaps), 0)
            self.assertAlmostEqual(sum(gaps) + len(centers) * 20, clear)

    def test_round_presets_emit_real_outer_envelope_copes_not_intersecting_tubes(self):
        preset = next(preset for preset in PRESETS if preset["id"] == "round-left-l")
        params = values(**preset["parameters"])
        built = MODULE.build_layout(params)
        self.assertTrue(any(part.clips for part in built.tubes))
        document = MODULE.generate(params, {})
        nodes = {node["key"]: node for node in document["geometry"]}
        for part in built.tubes:
            if not part.clips:
                continue
            node = nodes[part.key + ".finished"]
            self.assertEqual(node["operator"], "boolean")
            self.assertEqual(node["arguments"]["operation"], "subtract")
            for cutter in node["arguments"]["tools"]:
                transform = nodes[cutter]
                extrusion = nodes[transform["inputs"][0]]
                profile = nodes[extrusion["inputs"][0]]
                self.assertEqual(len(profile["arguments"]["contours"]), 1)

    def test_real_plates_have_no_fake_tube_and_do_not_overlap_bar_zone(self):
        for kind in ("plate", "lower_plate"):
            params = values(infillType=kind, installation="base_plate", layout="u", cornerPostMode="double")
            built = MODULE.build_layout(params)
            panels = [plate for plate in built.plates if plate.category == "plate.infill"]
            self.assertEqual(len(panels), len(built.bays))
            for panel in panels:
                for tube in built.tubes:
                    if tube.group == panel.group and tube.category == "guardrail.vertical_bar":
                        self.assertLess(panel.center[2] + panel.height / 2, tube.start[2])
            bases = [plate for plate in built.plates if plate.category == "plate.base"]
            self.assertEqual(len(bases), len(built.posts) - 2)
            self.assertTrue(all(len(plate.holes) == 4 for plate in bases))
            self.assertTrue(all(tube.start[2] == DEFAULTS["basePlateThickness"] for tube in built.tubes if tube.category == "guardrail.post"))

    def test_single_purpose_results_are_closed(self):
        for purpose in ("display", "manufacturing"):
            document = MODULE.generate(values(), {"geometryPurpose": purpose})
            self.assertEqual(document["extensions"]["tubeDesigner.geometryPurpose"], purpose)
            self.assertEqual(document["outputs"][0]["purpose"], "result")
            self.assertTrue(all(set(item["representations"]) == {"result"} for item in document["items"]))

    def test_invalid_dimensions_fail_before_geometry_is_emitted(self):
        for change in ({"guardHeight": float("nan")}, {"maximumVerticalClearGap": 0},
                       {"sideLength1": 100}, {"upperRailDrop": 10}, {"railWidth": 70},
                       {"infillType": "lower_plate", "lowerPanelHeight": 1400},
                       {"cornerPostMode": "double", "layout": "u", "sideLength2": 100}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                MODULE.build_layout(values(**change))

    def test_u_turn_rejects_wide_overlapping_caps_and_accepts_clear_separation(self):
        for side_length in (120, 240):
            with self.subTest(side_length=side_length), self.assertRaisesRegex(ValueError, "U型.*中心距"):
                MODULE.build_layout(values(layout="u", sideLength2=side_length, handrailWidth=200))
        # Centerline distance is 241 - 40 = 201; 200 mm wide caps have 1 mm clear.
        built = MODULE.build_layout(values(layout="u", sideLength2=241, handrailWidth=200))
        for first, second in combinations(built.tubes, 2):
            self.assertFalse(overlap(first, second), (first.key, second.key))

    def test_u_turn_allowance_includes_large_posts_and_real_baseplates(self):
        for parameters in (values(layout="u", sideLength2=120, largePostMode="ends"),
                           values(layout="u", sideLength2=140, installation="base_plate")):
            with self.subTest(parameters=parameters), self.assertRaisesRegex(ValueError, "U型.*中心距"):
                MODULE.build_layout(parameters)
        MODULE.build_layout(values(layout="u", sideLength2=121, largePostMode="ends"))
        MODULE.build_layout(values(layout="u", sideLength2=141, installation="base_plate"))

    def test_huge_dense_layout_is_stopped_before_any_geometry_graph_is_built(self):
        parameters = values(layout="u", sideLength1=30000, sideLength2=30000, sideLength3=30000,
                            maximumPostSpacing=1000, maximumVerticalClearGap=1,
                            infillWidth=3, infillDepth=3, infillWallThickness=0.5, infillCornerRadius=0)
        with patch.object(SUBJECT, "NeutralModel", side_effect=AssertionError("geometry must not start")):
            with self.assertRaisesRegex(ValueError, "超过2000件.*分段建模"):
                MODULE.generate(parameters, {})

    def test_total_part_budget_counts_panels_and_baseplates_and_allows_exact_limit(self):
        parameters = values(infillType="plate", installation="base_plate")
        built = MODULE.build_layout(parameters)
        total = len(built.tubes) + len(built.plates)
        self.assertGreater(len(built.plates), 0)
        with patch.object(SUBJECT, "MAX_PART_COUNT", total):
            MODULE.build_layout(parameters)
        with patch.object(SUBJECT, "MAX_PART_COUNT", total - 1):
            with self.assertRaisesRegex(ValueError, "管材和板件合计"):
                MODULE.build_layout(parameters)

    def test_all_twenty_default_presets_stay_below_budget_without_geometry_regressions(self):
        for preset in PRESETS:
            with self.subTest(preset=preset["id"]):
                built = MODULE.build_layout(values(**preset["parameters"]))
                self.assertLessEqual(len(built.tubes) + len(built.plates), SUBJECT.MAX_PART_COUNT)
                self.assertGreater(len(built.tubes), 0)


if __name__ == "__main__":
    unittest.main()
