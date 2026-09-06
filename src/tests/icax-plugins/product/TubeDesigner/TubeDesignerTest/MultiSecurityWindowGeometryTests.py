"""Geometric joint and dimensional regressions, without OCC or an application."""

from __future__ import annotations

from dataclasses import replace
import importlib.util
from itertools import combinations
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
SPEC = importlib.util.spec_from_file_location(
    "multi_security_window_geometry_test_subject",
    ROOT / "src/apps/tube-designer/templates/_shared/multi_face_security_window.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def parameters(**updates):
    # Fixed geometry inputs keep these regressions independent of UI presets.
    result = dict(productCode="TEST", frontWidth=1200.0, sideWidth=600.0,
                  leftWidth=600.0, rightWidth=600.0, depth=600.0, height=1800.0,
                  sidePosition="right", horizontalCount=4,
                  sideHorizontalCount=4, sideVerticalCount=4, sideMaximumVerticalClearGap=110.0,
                  frameCornerJoin="post_butt", mainHorizontalConnection="insert",
                  verticalLayoutMode="maximum_clear_gap", maximumVerticalClearGap=110.0,
                  verticalCountPerFace=4, firstHorizontalTopOffset=200.0,
                  lastHorizontalBottomOffset=200.0, topBottomCrossbarCount=2,
                  topBottomRodCount=4, horizontalBranchReserve=5.0,
                  verticalBranchReserve=10.0, assemblyClearance=0.1,
                  accessDoorEnabled=False, accessDoorFace="front", doorUOffset=80.0,
                  doorVOffset=500.0, doorWidth=400.0, doorHeight=600.0,
                  doorGap=3.0, doorHingeSide="left", doorHingeCount=2,
                  doorHorizontalCount=2, doorVerticalCount=2)
    for prefix, kind, width, depth, radius, wall in (
        ("frame", "rect", 38.0, 25.0, 2.0, 1.2),
        ("horizontal", "rect", 22.0, 22.0, 1.0, 1.0),
        ("vertical", "round", 19.0, 19.0, 0.0, 1.0),
        ("doorFrame", "rect", 25.0, 25.0, 2.0, 1.0),
        ("doorLeafFrame", "rect", 20.0, 20.0, 1.5, 1.0),
        ("doorHorizontal", "rect", 20.0, 20.0, 1.0, 0.8),
        ("doorVertical", "round", 16.0, 16.0, 0.0, 0.8),
    ):
        for suffix, value in (("ProfileType", kind), ("Width", width), ("Depth", depth),
                              ("CornerRadius", radius), ("WallThickness", wall)):
            result[prefix + suffix] = value
    result.update(updates)
    return result


def build(layout="three-face", purpose=None, **updates):
    recorded = []
    original = MODULE._emit_tube

    def emit(model, part, shared):
        recorded.append(part)
        return original(model, part, shared)

    context = {"template": {}}
    if purpose is not None:
        context["geometryPurpose"] = purpose
    with patch.object(MODULE, "_emit_tube", side_effect=emit):
        document = MODULE._generate_multi_face_geometry(parameters(**updates), context,
                                                       template_id="test", template_version="1", layout=layout)
    return document, recorded


def bounds(part):
    half = [abs(part.profile_x_axis[i]) * part.profile.depth / 2
            + abs(part.profile_y_axis[i]) * part.profile.width / 2 for i in range(3)]
    return ([min(part.start[i], part.end[i]) - half[i] for i in range(3)],
            [max(part.start[i], part.end[i]) + half[i] for i in range(3)])


def has_volume_overlap(left, right):
    low_a, high_a = bounds(left)
    low_b, high_b = bounds(right)
    return all(min(high_a[i], high_b[i]) - max(low_a[i], low_b[i]) > 1e-7 for i in range(3))


def envelope(parts):
    limits = [bounds(part) for part in parts]
    return [max(high[i] for _, high in limits) - min(low[i] for low, _ in limits) for i in range(3)]


def cross2(first, second):
    return first[0] * second[1] - first[1] * second[0]


def signed_area(polygon):
    return sum(cross2(first, second) for first, second in zip(polygon, polygon[1:] + polygon[:1])) / 2


def clip_half_plane(polygon, first, second, sign=1.0):
    """Independent planar evaluator for the emitted end-cut polygon edges."""
    if not polygon:
        return []
    vector = (second[0] - first[0], second[1] - first[1])

    def distance(point):
        return sign * cross2(vector, (point[0] - first[0], point[1] - first[1]))

    result = []
    for start, end in zip(polygon, polygon[1:] + polygon[:1]):
        a, b = distance(start), distance(end)
        if a >= -1.0e-8:
            result.append(start)
        if (a < -1.0e-8 and b > 1.0e-8) or (a > 1.0e-8 and b < -1.0e-8):
            factor = a / (a - b)
            result.append(tuple(start[i] + factor * (end[i] - start[i]) for i in range(2)))
    return result


def manufactured_footprint(part, document):
    low, high = bounds(part)
    polygon = [(low[0], low[1]), (high[0], low[1]), (high[0], high[1]), (low[0], high[1])]
    nodes = {node["key"]: node for node in document["geometry"]}
    for end in ("start", "end"):
        node = nodes.get(f"{part.key}.miter.{end}.profile")
        if node is None:
            continue
        arguments = node["arguments"]
        placement = arguments["placement"]
        world = [tuple(placement["origin"][i] + placement["xAxis"][i] * point[0]
                       + placement["yAxis"][i] * point[1] for i in range(2))
                 for point in arguments["contours"][0]["points"]]
        first, second = world[1:3]
        cutter_side = cross2((second[0] - first[0], second[1] - first[1]),
                             (world[0][0] - first[0], world[0][1] - first[1]))
        polygon = clip_half_plane(polygon, first, second, -1.0 if cutter_side > 0 else 1.0)
    return polygon


def intersection_area(first, second):
    result = first
    sign = 1.0 if signed_area(second) > 0 else -1.0
    for a, b in zip(second, second[1:] + second[:1]):
        result = clip_half_plane(result, a, b, sign)
    return abs(signed_area(result))


class MultiSecurityWindowGeometryTests(unittest.TestCase):
    def assert_sizes(self, actual, expected):
        for first, second in zip(actual, expected):
            self.assertAlmostEqual(first, second, places=7)

    def test_outside_dimensions_match_actual_frame_for_all_layouts(self):
        for layout in ("two-face", "three-face", "five-face"):
            for side in ("left", "right"):
                with self.subTest(layout=layout, side=side):
                    self.assert_sizes(envelope(build(layout, sidePosition=side)[1]),
                                      (1200, 600, 1800))
        self.assert_sizes(envelope(build(rightWidth=900.0)[1]),
                          (1200, 900, 1800))

    def test_outer_frame_stops_at_posts_independently_of_grid_insertion(self):
        for layout in ("two-face", "three-face", "five-face"):
            _, parts = build(layout)
            frame = [part for part in parts if part.key.startswith("outer_frame.")]
            for first, second in combinations(frame, 2):
                self.assertFalse(has_volume_overlap(first, second), (layout, first.key, second.key))
            changed = {part.key: part for part in build(layout, horizontalBranchReserve=0.0)[1]}
            for part in frame:
                self.assertEqual((part.start, part.end), (changed[part.key].start, changed[part.key].end))

    def test_fixed_and_leaf_frame_corners_are_nonoverlapping_square_butts(self):
        for face in ("front", "left", "right", "top", "bottom"):
            document, parts = build("five-face", accessDoorEnabled=True, accessDoorFace=face,
                                    doorVOffset=80.0, doorHeight=400.0)
            for prefix in ("access_door.fixed_frame.", "access_door.leaf.frame."):
                frame = [part for part in parts if part.key.startswith(prefix)]
                self.assertEqual(4, len(frame))
                for first, second in combinations(frame, 2):
                    self.assertFalse(has_volume_overlap(first, second), (face, first.key, second.key))
            for item in document["items"]:
                if item["key"].startswith("access_door.fixed_frame."):
                    self.assertEqual("butt_90", item["properties"]["tubeDesigner.connectionProcess"]["cornerJoin"])

    def test_clipped_main_rods_meet_fixed_frame_without_volume_overlap(self):
        for face in ("front", "left", "right", "top", "bottom"):
            _, parts = build("five-face", accessDoorEnabled=True, accessDoorFace=face,
                             doorVOffset=80.0, doorHeight=400.0)
            fixed = [part for part in parts if part.key.startswith("access_door.fixed_frame.")]
            grid = [part for part in parts if part.key.startswith(("main_grid.", "cap_grid."))]
            for frame in fixed:
                for rod in grid:
                    self.assertFalse(has_volume_overlap(frame, rod), (face, frame.key, rod.key))

    def test_leaf_horizontal_butts_and_vertical_has_real_insert_holes(self):
        document, parts = build(accessDoorEnabled=True)
        by_key = {part.key: part for part in parts}
        nodes = {node["key"]: node for node in document["geometry"]}
        items = {item["key"]: item for item in document["items"]}
        for item in document["items"]:
            if item["key"].startswith("access_door.leaf.horizontal."):
                for side in ("left", "right"):
                    self.assertFalse(has_volume_overlap(by_key[item["key"]], by_key[f"access_door.leaf.frame.{side}.0001"]))
        for side in ("bottom", "top"):
            item = items[f"access_door.leaf.frame.{side}.0001"]
            self.assertEqual("boolean", nodes[item["representations"]["export"]]["operator"])
            self.assertEqual(2, len(item["properties"]["tubeDesigner.connectionProcess"]["receives"]))

    def test_frame_receiver_fit_is_checked_only_for_actual_connections(self):
        with self.assertRaisesRegex(ValueError, "内腔"):
            build(frameWallThickness=2.0)
        build(frameWallThickness=2.0, horizontalCount=0, sideHorizontalCount=0)
        build(frameWallThickness=2.0, horizontalBranchReserve=0.0)

    def test_no_insertion_produces_no_boundary_drill_holes(self):
        document, _ = build(horizontalBranchReserve=0.0, verticalBranchReserve=0.0,
                            horizontalCount=0)
        for item in document["items"]:
            if item["key"].startswith("outer_frame."):
                self.assertEqual([], item["properties"]["tubeDesigner.connectionProcess"]["receives"])

    def test_dense_and_out_of_bounds_grids_are_rejected(self):
        cases = (dict(horizontalCount=100),
                 dict(verticalLayoutMode="manual_count", verticalCountPerFace=100),
                 dict(firstHorizontalTopOffset=0.0), dict(lastHorizontalBottomOffset=0.0),
                 dict(topBottomCrossbarCount=100),
                 dict(verticalLayoutMode="manual_count", topBottomRodCount=100),
                 dict(accessDoorEnabled=True, doorHorizontalCount=100),
                 dict(accessDoorEnabled=True, doorVerticalCount=100))
        for updates in cases:
            with self.subTest(updates=updates), self.assertRaisesRegex(ValueError, "布置过密|超出"):
                build("five-face", **updates)

    def test_door_outer_envelope_must_fit_inside_the_main_frame(self):
        with self.assertRaisesRegex(ValueError, "外框以内"):
            build(accessDoorEnabled=True, doorUOffset=30.0)

    def test_neighboring_inserted_rail_ends_must_not_collide_inside_corner_post(self):
        for layout in ("two-face", "three-face", "five-face"):
            with self.subTest(layout=layout), self.assertRaisesRegex(ValueError, "端部相交"):
                build(layout, horizontalBranchReserve=10.0)

    def test_door_offsets_measure_the_fixed_frame_outside_edges(self):
        for face in ("front", "top", "bottom"):
            _, parts = build("five-face", accessDoorEnabled=True, accessDoorFace=face,
                             doorUOffset=80.0, doorVOffset=80.0, doorHeight=400.0)
            fixed = [part for part in parts if part.key.startswith("access_door.fixed_frame.")]
            self.assertAlmostEqual(-600 + 80, min(bounds(part)[0][0] for part in fixed))
            if face == "front":
                self.assertAlmostEqual(80, min(bounds(part)[0][2] for part in fixed))
            else:
                self.assertAlmostEqual(-80, max(bounds(part)[1][1] for part in fixed))

    def test_cap_automatic_count_and_maximum_clear_gap_are_preserved(self):
        _, parts = build("five-face")
        rods = [part for part in parts if part.key.startswith("cap_grid.vertical.") and part.face_index == 4]
        self.assertEqual(9, len(rods))
        positions = sorted(part.start[0] for part in rods)
        gaps = [positions[0] - 19 / 2 - (-600 + 19 + 25 / 2),
                600 - 19 - 25 / 2 - positions[-1] - 19 / 2]
        gaps.extend(right - left - 19 for left, right in zip(positions, positions[1:]))
        self.assertLessEqual(max(gaps), 110.0)
        self.assertAlmostEqual(104.2, max(gaps))

    def test_rectangular_vertical_section_keeps_width_along_spacing_axis(self):
        _, parts = build(verticalProfileType="rect", verticalWidth=19.0, verticalDepth=15.0,
                         verticalCornerRadius=1.0)
        front = next(part for part in parts if part.key.startswith("main_grid.vertical.") and part.face_index == 2)
        low, high = bounds(front)
        self.assertAlmostEqual(19.0, high[0] - low[0])
        self.assertAlmostEqual(15.0, high[1] - low[1])

    def test_purpose_separation_and_shared_tube_prototypes_survive_joint_fixes(self):
        display, _ = build("five-face", purpose="display", accessDoorEnabled=True,
                           doorVOffset=80.0, doorHeight=400.0)
        manufactured, _ = build("five-face", purpose="manufacturing", accessDoorEnabled=True,
                                doorVOffset=80.0, doorHeight=400.0)
        self.assertNotIn("boolean", {node["operator"] for node in display["geometry"]})
        self.assertIn("boolean", {node["operator"] for node in manufactured["geometry"]})
        transforms = [node for node in display["geometry"] if node["operator"] == "transform"]
        self.assertLess(len({node["inputs"][0] for node in transforms}), len(transforms))
        self.assertEqual({item["key"] for item in display["items"]},
                         {item["key"] for item in manufactured["items"]})

    def test_miter_frames_preserve_outside_envelope_and_long_point_stock_lengths(self):
        for layout in ("two-face", "three-face", "five-face"):
            for side in ("left", "right"):
                with self.subTest(layout=layout, side=side):
                    document, parts = build(layout, frameCornerJoin="rail_miter", frameDepth=38.0,
                                            sidePosition=side)
                    frame = [part for part in parts if part.key.startswith("outer_frame.")]
                    self.assert_sizes(envelope(frame), (1200.0, 600.0, 1800.0))
                    items = {item["key"]: item for item in document["items"]}
                    for part in frame:
                        if ".vertical." in part.key:
                            self.assertEqual((38.0, 1762.0), (part.start[2], part.end[2]))
                            self.assertAlmostEqual(1724.0, part.length)
                        else:
                            expected = 1200.0 if part.face_name in ("正面", "上面", "下面") else 600.0
                            self.assertAlmostEqual(expected, part.length)
                            cut_count = int(part.start_miter is not None) + int(part.end_miter is not None)
                            polygon = manufactured_footprint(part, document)
                            self.assertAlmostEqual((expected - 19.0 * cut_count) * 38.0, abs(signed_area(polygon)))
                        self.assertEqual(round(part.length, 3), items[part.key]["properties"]["length"])
        self.assert_sizes(envelope(build(frameCornerJoin="rail_miter", frameDepth=38.0,
                                         leftWidth=800.0, rightWidth=900.0)[1]), (1200, 900, 1800))

    def test_miter_cut_polygons_partition_corner_without_overlap_or_open_seam(self):
        for layout in ("two-face", "three-face", "five-face"):
            for side in ("left", "right"):
                document, parts = build(layout, frameCornerJoin="rail_miter", frameDepth=38.0,
                                        sidePosition=side)
                rails = [part for part in parts if part.key.startswith("outer_frame.")
                         and ".vertical." not in part.key and part.start[2] == 19.0]
                polygons = {part.key: manufactured_footprint(part, document) for part in rails}
                seam_count = 0
                for first, second in combinations(rails, 2):
                    a, b = polygons[first.key], polygons[second.key]
                    self.assertAlmostEqual(0.0, intersection_area(a, b), places=6)
                    common = [point for point in a if any(math.dist(point, other) < 1.0e-7 for other in b)]
                    if common:
                        self.assertEqual(2, len(common), (layout, first.key, second.key, common))
                        self.assertAlmostEqual(38.0 * math.sqrt(2), math.dist(*common), places=6)
                        seam_count += 1
                self.assertEqual({"two-face": 1, "three-face": 2, "five-face": 4}[layout], seam_count)
                if layout == "five-face":
                    self.assertAlmostEqual(1200 * 600 - (1200 - 76) * (600 - 76),
                                           sum(abs(signed_area(poly)) for poly in polygons.values()))

    def test_miter_manufacturing_cuts_real_tubes_but_display_stays_lightweight(self):
        for purpose in ("display", "manufacturing"):
            document, parts = build("five-face", purpose=purpose,
                                    frameCornerJoin="rail_miter", frameDepth=38.0)
            nodes = {node["key"]: node for node in document["geometry"]}
            cut_profiles = [node for key, node in nodes.items() if ".miter." in key and key.endswith(".profile")]
            self.assertEqual(0 if purpose == "display" else 16, len(cut_profiles))
            if purpose == "display":
                self.assertNotIn("boolean", {node["operator"] for node in nodes.values()})
            else:
                for part in parts:
                    if part.start_miter is None and part.end_miter is None:
                        continue
                    end_cut = nodes[f"{part.key}.solid.miter"]
                    self.assertEqual("subtract", end_cut["arguments"]["operation"])
                    self.assertEqual(part.key + ".solid", end_cut["arguments"]["target"])
                    target = nodes[end_cut["arguments"]["target"]]
                    self.assertEqual("transform", target["operator"])
                    extrusion = nodes[target["inputs"][0]]
                    profile = nodes[extrusion["inputs"][0]]
                    self.assertEqual(2, len(profile["arguments"]["contours"]))
                    self.assertEqual("roundedRectangle", profile["arguments"]["contours"][0]["kind"])
            for item in document["items"]:
                if item["key"].startswith(("outer_frame.top.", "outer_frame.bottom.", "outer_frame.back.")):
                    self.assertEqual("uncut_miter_stock", item["properties"]["tubeDesigner.displayApproximation"])

    def test_miter_rejects_unsupported_profiles_and_holes_that_break_into_end_cuts(self):
        for updates in (dict(), dict(frameProfileType="round", frameWidth=38.0, frameDepth=38.0)):
            with self.assertRaisesRegex(ValueError, "仅支持.*方管"):
                build(frameCornerJoin="rail_miter", **updates)
        # This row fits the uncut tube envelope, but its cutter crosses the
        # diagonal seam. The equivalent straight-butt cap has no diagonal seam.
        probe = dict(frameDepth=38.0, topBottomCrossbarCount=12,
                     horizontalCount=0, sideHorizontalCount=0,
                     verticalLayoutMode="manual_count", verticalCountPerFace=0,
                     sideVerticalCount=0, topBottomRodCount=0)
        # A direct boundary probe avoids arbitrary extra clearance requirements:
        # a transverse hole exactly at the corner center necessarily opens the cut.
        document, parts = build("five-face", frameCornerJoin="rail_miter", **probe)
        receiver = next(part for part in parts if part.key.startswith("outer_frame.top.") and part.face_name == "左侧面")
        inserted = next(part for part in parts if part.key.startswith("cap_grid.horizontal.") and part.face_index == 4)
        shifted = replace(inserted, start=(inserted.start[0], -30.0, inserted.start[2]),
                          end=(inserted.end[0], -30.0, inserted.end[2]))
        with self.assertRaisesRegex(ValueError, "进入45°斜切端面"):
            MODULE._validate_miter_hole_margin(receiver, shifted, 0.1)

    def test_welded_horizontal_ends_touch_frame_and_do_not_drill_it(self):
        for layout in ("two-face", "three-face", "five-face"):
            for join in ("post_butt", "rail_miter"):
                document, parts = build(layout, frameCornerJoin=join, frameDepth=38.0,
                                        mainHorizontalConnection="weld", horizontalBranchReserve=-1)
                frame = [part for part in parts if part.key.startswith("outer_frame.")]
                horizontals = [part for part in parts if part.key.startswith(("main_grid.horizontal.", "cap_grid.horizontal."))]
                horizontal_keys = {part.key for part in horizontals}
                for item in document["items"]:
                    process = item["properties"]["tubeDesigner.connectionProcess"]
                    if item["key"].startswith("outer_frame."):
                        self.assertFalse(horizontal_keys.intersection(process["receives"]))
                    elif item["key"] in horizontal_keys:
                        self.assertEqual("weld", process["outerFrameConnection"])
                        self.assertEqual(0.0, process["outerFrameInsertionDepth"])
                        self.assertTrue(process["receives"], item["key"])
                for first in frame:
                    for second in horizontals:
                        self.assertFalse(has_volume_overlap(first, second), (first.key, second.key))

    def test_unused_horizontal_reserve_does_not_change_welded_geometry(self):
        baseline, baseline_parts = build("five-face", mainHorizontalConnection="weld", frameDepth=38.0)
        for reserve in (-1, 0, 20, 1000, "unused"):
            changed, changed_parts = build("five-face", mainHorizontalConnection="weld", frameDepth=38.0,
                                          horizontalBranchReserve=reserve)
            self.assertEqual(baseline_parts, changed_parts)
            self.assertEqual(baseline["geometry"], changed["geometry"])
        for key in ("horizontalBranchReserve", "verticalBranchReserve"):
            with self.assertRaisesRegex(ValueError, "0到20"):
                build(**{key: -1})
        with self.assertRaisesRegex(ValueError, "0到20"):
            build(mainHorizontalConnection="weld", frameDepth=38.0, verticalBranchReserve=-1)

    def test_weld_rejects_curved_frames_and_insufficient_flat_receiving_faces(self):
        for layout in ("two-face", "three-face", "five-face"):
            with self.subTest(layout=layout):
                with self.assertRaisesRegex(ValueError, "标准矩形管外框"):
                    build(layout, mainHorizontalConnection="weld", frameProfileType="round",
                          frameWidth=38.0, frameDepth=38.0)
                with self.assertRaisesRegex(ValueError, "平直面宽度 20"):
                    build(layout, mainHorizontalConnection="weld", frameDepth=38.0,
                          frameCornerRadius=9.0)
                # Width alone would pass (38 - 2*2), but the other side of
                # the corner post only has 25 - 2*2 = 21 mm of flat surface.
                with self.assertRaisesRegex(ValueError, "平直面宽度 21"):
                    build(layout, mainHorizontalConnection="weld", frameDepth=25.0,
                          frameCornerRadius=2.0)
                # Both flat sides exactly accommodate the 22 mm-deep end.
                build(layout, mainHorizontalConnection="weld", frameDepth=25.0,
                      frameCornerRadius=1.5)
                # Insertion is a different actual joint, not a flat butt seat.
                build(layout, mainHorizontalConnection="insert", frameDepth=25.0,
                      frameCornerRadius=2.0)

    def test_miter_and_weld_keep_all_five_opening_surfaces_clear(self):
        for face in ("front", "left", "right", "top", "bottom"):
            document, parts = build("five-face", frameCornerJoin="rail_miter", frameDepth=38.0,
                                    mainHorizontalConnection="weld", accessDoorEnabled=True,
                                    accessDoorFace=face, doorVOffset=80.0, doorHeight=400.0)
            fixed = [part for part in parts if part.key.startswith("access_door.fixed_frame.")]
            grid = [part for part in parts if part.key.startswith(("main_grid.", "cap_grid."))]
            for frame in fixed:
                for rod in grid:
                    self.assertFalse(has_volume_overlap(frame, rod), (face, frame.key, rod.key))
            for item in document["items"]:
                if item["key"].startswith(("main_grid.horizontal.start.", "main_grid.horizontal.end.",
                                            "cap_grid.horizontal.start.", "cap_grid.horizontal.end.")):
                    process = item["properties"]["tubeDesigner.connectionProcess"]
                    self.assertEqual("butt_to_fixed_frame_outer_face", process["openingEndJoin"])
                    self.assertEqual([], process["passesInto"])

    def test_new_options_keep_exact_input_identity(self):
        values = parameters(frameCornerJoin="rail_miter", frameDepth=38.0,
                            mainHorizontalConnection="weld", horizontalBranchReserve=-1,
                            sideHorizontalCount=2, sideMaximumVerticalClearGap=80.0)
        original = dict(values)
        document = MODULE._generate_multi_face_geometry(values, {"template": {}},
                                                        template_id="test", template_version="1", layout="three-face")
        self.assertEqual(original, values)
        self.assertEqual(original, document["parameters"])

    def test_front_and_side_manual_grid_counts_are_independent(self):
        for layout in ("two-face", "three-face", "five-face"):
            for side in ("left", "right"):
                _, parts = build(layout, sidePosition=side, horizontalCount=1, sideHorizontalCount=3,
                                 verticalLayoutMode="manual_count", verticalCountPerFace=2, sideVerticalCount=5)
                for name in {part.face_name for part in parts if part.key.startswith("main_grid.")}:
                    front = name == "正面"
                    self.assertEqual(1 if front else 3, len([part for part in parts
                                     if part.face_name == name and part.key.startswith("main_grid.horizontal.")]))
                    self.assertEqual(2 if front else 5, len([part for part in parts
                                     if part.face_name == name and part.key.startswith("main_grid.vertical.")]))

    def test_side_clear_gap_is_independent_of_front_and_cap_clear_gap(self):
        _, first = build("five-face")
        _, second = build("five-face", sideMaximumVerticalClearGap=60.0)
        for prefix, face in (("main_grid.vertical.", "正面"), ("cap_grid.vertical.", "上面")):
            self.assertEqual([replace(part, key="") for part in first if part.key.startswith(prefix) and part.face_name == face],
                             [replace(part, key="") for part in second if part.key.startswith(prefix) and part.face_name == face])
        for name in ("左侧面", "右侧面"):
            old = [part for part in first if part.key.startswith("main_grid.vertical.") and part.face_name == name]
            new = [part for part in second if part.key.startswith("main_grid.vertical.") and part.face_name == name]
            self.assertGreater(len(new), len(old))
            positions = sorted(part.start[1] for part in new)
            self.assertLessEqual(max(right - left - 19.0 for left, right in zip(positions, positions[1:])), 60.0)
        for updates in (dict(sideHorizontalCount=101), dict(sideVerticalCount=-1),
                        dict(sideMaximumVerticalClearGap=0.0), dict(frameCornerJoin="unknown"),
                        dict(mainHorizontalConnection="unknown")):
            with self.assertRaises(ValueError):
                build(**updates)


if __name__ == "__main__":
    unittest.main()
