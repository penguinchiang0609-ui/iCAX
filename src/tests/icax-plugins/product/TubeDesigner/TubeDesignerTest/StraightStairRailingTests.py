"""Straight-railing gap regressions without importing any native geometry DLL."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest

ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template

DIRECTORY = ROOT / "src/apps/tube-designer/templates/straight_stair_railing"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {definition["key"]: definition["defaultValue"] for definition in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "stair-railing-gap-regressions")


def values(**changes):
    return {**DEFAULTS, **changes}


class StraightStairRailingTests(unittest.TestCase):
    def assert_clearances(self, parameters):
        parts = MODULE._build_parts(parameters)
        posts = [part for part in parts if part.category_key == "stair.post"]
        bars = [part for part in parts if part.category_key == "stair.vertical_infill"]
        for bar in bars:
            low, high = bar.start[0] - bar.profile.depth / 2, bar.start[0] + bar.profile.depth / 2
            for post in posts:
                post_low, post_high = post.start[0] - post.profile.depth / 2, post.start[0] + post.profile.depth / 2
                self.assertFalse(min(high, post_high) - max(low, post_low) > 1e-7,
                                 (bar.key, post.key, parameters))
        for left, right in zip(posts, posts[1:]):
            low, high = left.start[0] + left.profile.depth / 2, right.start[0] - right.profile.depth / 2
            inside = sorted([part for part in bars if low < part.start[0] < high], key=lambda part: part.start[0])
            last = low
            gaps = []
            for bar in inside:
                gaps.append(bar.start[0] - bar.profile.depth / 2 - last)
                last = bar.start[0] + bar.profile.depth / 2
            gaps.append(high - last)
            self.assertAlmostEqual(min(gaps), max(gaps))
            if parameters["verticalLayoutMode"] == "maximum_clear_gap":
                self.assertLessEqual(max(gaps), parameters["maximumVerticalClearGap"] + 1e-7)
        return parts, bars

    def test_default_is_twenty_one_bars_in_three_independent_bays(self):
        parts, bars = self.assert_clearances(values())
        self.assertEqual(len(parts), 27)
        self.assertEqual(len(bars), 21)
        for post_x in (0, 1000, 2000, 3000):
            self.assertFalse(any(abs(bar.start[0] - post_x) < 30 for bar in bars))

    def test_nonsquare_sections_use_depth_for_world_x_after_axis_swap(self):
        for post_depth, bar_depth in ((80, 8), (80, 40), (30, 60)):
            with self.subTest(post_depth=post_depth, bar_depth=bar_depth):
                self.assert_clearances(values(postWidth=40, postDepth=post_depth,
                                               infillWidth=20, infillDepth=bar_depth))

    def test_manual_quantity_is_a_total_and_never_crosses_posts(self):
        for count in (0, 1, 2, 3, 5, 18, 20, 50):
            with self.subTest(count=count):
                _, bars = self.assert_clearances(values(verticalLayoutMode="manual_count", verticalBarCount=count))
                self.assertEqual(len(bars), count)

    def test_manual_remainder_is_stable_and_center_first(self):
        parameters = values(verticalLayoutMode="manual_count", verticalBarCount=4)
        bays = MODULE._vertical_bays(parameters, MODULE._profile(parameters, "post"), MODULE._profile(parameters, "infill"))
        self.assertEqual([len(bay.positions) for bay in bays], [1, 2, 1])

    def test_too_small_manual_count_warns_without_changing_the_user_count(self):
        document = MODULE.generate(values(verticalLayoutMode="manual_count", verticalBarCount=18), {"template": {}})
        warning = next(item for item in document["diagnostics"] if item["code"] == "stair-railing.vertical-clear-gap")
        self.assertEqual(warning["severity"], "warning")
        self.assertIn("120.00", warning["message"])
        self.assertEqual(len([item for item in document["items"] if item["properties"]["manufacturing.categoryKey"] == "stair.vertical_infill"]), 18)

    def test_adequate_manual_count_and_automatic_mode_have_no_gap_warning(self):
        for parameters in (values(), values(verticalLayoutMode="manual_count", verticalBarCount=21)):
            document = MODULE.generate(parameters, {"template": {}})
            self.assertFalse(any(item["code"] == "stair-railing.vertical-clear-gap" for item in document["diagnostics"]))

    def test_all_existing_builtin_profile_types_keep_their_geometry_contract(self):
        for kind in ("rect", "round", "ellipse", "flat-oval", "polygon"):
            with self.subTest(kind=kind):
                parameters = values(postProfileType=kind, infillProfileType=kind, handrailProfileType=kind)
                self.assert_clearances(parameters)
                document = MODULE.generate(parameters, {"template": {}, "geometryPurpose": "manufacturing"})
                self.assertEqual(document["extensions"]["tubeDesigner.geometryPurpose"], "manufacturing")
                self.assertEqual(document["template"]["version"], "1.1.0")
                self.assertTrue(all(item["properties"]["tubeDesigner.profile"]["kind"] == kind for item in document["items"]))

    def test_star_polygon_bars_remain_supported(self):
        self.assert_clearances(values(postProfileType="polygon", postShapeMode="star", postSideCount=5,
                                       infillProfileType="polygon", infillShapeMode="star", infillSideCount=5,
                                       infillWallThickness=0.8))

    def test_extension_labels_match_the_existing_slope_geometry(self):
        definitions = {definition["key"]: definition for definition in DESCRIPTOR["parameters"]}
        for key in ("startExtension", "endExtension"):
            self.assertIn("沿坡延伸", definitions[key]["displayName"]["zh-CN"])
            self.assertIn("水平投影", definitions[key]["displayName"]["zh-CN"])
        self.assertIn("中心线", definitions["railingHeight"]["displayName"]["zh-CN"])
        parts = MODULE._build_parts(values())
        cap = parts[0]
        self.assertAlmostEqual(cap.start[2], 950 - 150 * 1800 / 3000)
        self.assertAlmostEqual(cap.end[2], 950 + 3150 * 1800 / 3000)

    def test_impossible_rod_and_post_counts_fail_clearly(self):
        for change in ({"postCount": 50, "flightRun": 300},
                       {"verticalLayoutMode": "manual_count", "verticalBarCount": 200},
                       {"maximumVerticalClearGap": 0}, {"startExtension": -1}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                MODULE._build_parts(values(**change))


if __name__ == "__main__":
    unittest.main()
