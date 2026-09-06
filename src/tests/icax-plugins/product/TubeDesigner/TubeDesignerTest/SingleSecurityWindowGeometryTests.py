"""Single-face frame, insertion and unfolding regressions without OCC or UI."""

from __future__ import annotations

import importlib.util
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / "src/apps/tube-designer/templates").is_dir())
sys.path.insert(0, str(ROOT / "src/iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_sdk import NeutralModel

PACKAGE = ROOT / "src/apps/tube-designer/templates/single_face_security_window"
SPEC = importlib.util.spec_from_file_location("single_window_geometry_subject", PACKAGE / "template.py")
assert SPEC is not None and SPEC.loader is not None
SUBJECT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUBJECT
SPEC.loader.exec_module(SUBJECT)


def parameters(**overrides):
    descriptor = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
    result = {parameter["key"]: parameter["defaultValue"] for parameter in descriptor["parameters"]}
    # Explicit fixture dimensions, independent of commercial default presets.
    result.update(width=1200.0, height=1800.0, frameLayout="left_right",
                  horizontalCount=4, verticalLayoutMode="maximum_clear_gap",
                  maximumVerticalClearGap=110.0, middleVerticalCount=9,
                  firstHorizontalTopOffset=200.0, lastHorizontalBottomOffset=200.0,
                  frameWidth=38.0, frameDepth=25.0, frameWallThickness=1.2,
                  horizontalWidth=22.0, horizontalDepth=22.0, horizontalWallThickness=1.0,
                  verticalWidth=19.0, verticalDepth=19.0, verticalWallThickness=1.0,
                  horizontalBranchReserve=10.0, verticalBranchReserve=10.0,
                  mainHorizontalConnection="insert", frameProfileType="rect",
                  horizontalProfileType="rect", verticalProfileType="round",
                  frameCornerRadius=2.0, horizontalCornerRadius=2.0,
                  verticalCornerRadius=0.0, vGrooveKFactor=0.62,
                  assemblyClearance=0.1, accessDoorEnabled=False,
                  doorWidth=300.0, doorHeight=360.0, doorLeft=450.0, doorBottom=700.0,
                  doorGap=6.0, doorFrameWidth=25.0, doorFrameDepth=25.0,
                  doorFrameWallThickness=1.0, doorLeafFrameWidth=20.0,
                  doorLeafFrameDepth=20.0, doorLeafFrameWallThickness=1.0,
                  doorHorizontalCount=1, doorVerticalCount=1,
                  doorHorizontalWidth=20.0, doorHorizontalDepth=20.0,
                  doorHorizontalWallThickness=0.8, doorVerticalWidth=16.0,
                  doorVerticalWallThickness=0.8,
                  frameJoinType="v_groove_90:sharp_v", doorFrameJoinType="v_groove_90:sharp_v",
                  doorLeafFrameJoinType="v_groove_90:sharp_v")
    result.update(overrides)
    return result


def generate(purpose="manufacturing", **overrides):
    return SUBJECT._generate_geometry(parameters(**overrides), {"template": {}, "geometryPurpose": purpose})


def graph(document):
    return {node["key"]: node for node in document["geometry"]}


class SingleSecurityWindowGeometryTests(unittest.TestCase):
    def test_catalog_plate_presets_build_with_public_default_opening(self):
        descriptor = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
        defaults = {parameter["key"]: parameter["defaultValue"] for parameter in descriptor["parameters"]}
        for preset in descriptor["extensions"]["catalog"]["presets"]:
            with self.subTest(preset=preset["id"]):
                document = SUBJECT.generate({**defaults, **preset["parameters"]},
                                             {"template": {}, "geometryPurpose": "manufacturing"})
                plates = [item for item in document["items"]
                          if item["properties"].get("manufacturing.partKind") == "plate"]
                self.assertEqual(0 if preset["id"] == "tube-grid" else 1, len(plates))

    def test_center_plate_is_a_separate_solid_with_real_thickness_and_no_tube_profile(self):
        document = generate(mainInfillMode="center_plate")
        plates = [item for item in document["items"]
                  if item["properties"].get("manufacturing.partKind") == "plate"]
        self.assertEqual(1, len(plates))
        plate = plates[0]
        self.assertNotIn("tubeDesigner.profile", plate["properties"])
        self.assertEqual({"width": 300.0, "height": 600.0, "thickness": 2.0, "areaMm2": 180000.0},
                         plate["properties"]["manufacturing.plate"])
        nodes = graph(document)
        solid = nodes[plate["representations"]["result"]]
        self.assertEqual("extrude", solid["operator"])
        self.assertEqual([0.0, -2.0, 0.0], solid["arguments"]["vector"])
        outline = nodes[solid["inputs"][0]]["arguments"]["contours"]
        self.assertEqual(1, len(outline), "A plate must have one solid outer contour, not a tube cavity")
        self.assertEqual(4, len([item for item in document["items"] if item["key"].startswith("center_plate.frame.")]))
        self.assertTrue(any(".panel_before" in item["key"] for item in document["items"]))
        self.assertTrue(any(".panel_after" in item["key"] for item in document["items"]))

    def test_center_plate_splits_grid_without_any_tube_through_the_plate(self):
        captured = []
        original = SUBJECT._emit_tube_geometry

        def observe(model, part, shared):
            captured.append(part)
            return original(model, part, shared)

        with patch.object(SUBJECT, "_emit_tube_geometry", observe):
            generate(mainInfillMode="center_plate")
        for part in captured:
            if not part.key.startswith("main_grid."):
                continue
            if part.vertical and 450 <= part.start[0] <= 750:
                self.assertTrue(part.end[2] <= 578 or part.start[2] >= 1222, part.key)
            elif not part.vertical and 600 <= part.start[2] <= 1200:
                self.assertTrue(part.end[0] <= 428 or part.start[0] >= 772, part.key)

    def test_panel_is_inside_opening_leaf_and_invalid_panel_does_not_escape_boundary(self):
        document = generate(mainInfillMode="center_plate", accessDoorEnabled=True,
                            centerPlateWidth=100.0, centerPlateHeight=180.0)
        plate = next(item for item in document["items"] if item["key"] == "center_plate.panel.0001")
        self.assertEqual("access_door.leaf", plate["properties"]["group"])
        with self.assertRaisesRegex(ValueError, "超出可用净空"):
            generate(mainInfillMode="center_plate", centerPlateWidth=1200.0)
        with self.assertRaisesRegex(ValueError, "板厚"):
            generate(mainInfillMode="center_plate", centerPlateThickness=0.0)

    def test_plate_helper_declares_actual_round_hole_cutters(self):
        model = NeutralModel(template_id="plate-test", template_version="1.0.0",
                             package_digest="test", parameters={})
        geometry = SUBJECT.emit_rectangular_plate(model, "p", width=100, height=200, thickness=3,
                                                  holes=[{"x": 20, "y": 30, "diameter": 8}])
        self.assertEqual("p.finished", geometry)
        with self.assertRaisesRegex(ValueError, "板孔"):
            SUBJECT.emit_rectangular_plate(model, "invalid", width=100, height=200, thickness=3,
                                           holes=[{"x": 49, "y": 0, "diameter": 8}])

    def test_default_spacing_and_existing_side_frame_piercings(self):
        document = generate()
        self.assertEqual(15, len(document["items"]))
        profiles = [node for node in document["geometry"]
                    if node["key"].startswith("outer_frame.") and ".through." in node["key"]
                    and node["operator"] == "profile2d"]
        self.assertEqual(8, len(profiles))
        step = (1200.0 - 2 * 38.0) / 10
        self.assertAlmostEqual(102.9, step - 19.0 / 2)
        self.assertAlmostEqual(93.4, step - 19.0)

    def test_gap_is_between_actual_frame_and_leaf_surfaces(self):
        for gap in (0.0, 2.0, 6.0):
            with self.subTest(gap=gap):
                nodes = graph(generate("display", accessDoorEnabled=True, doorGap=gap))
                fixed = nodes["access_door.fixed_frame.continuous.0001.display.left.solid"]["arguments"]["placement"]["origin"]
                leaf = nodes["access_door.leaf.frame.continuous.0001.display.left.solid"]["arguments"]["placement"]["origin"]
                self.assertAlmostEqual(gap, (leaf[0] - 10.0) - (fixed[0] + 12.5))
                self.assertAlmostEqual(gap, leaf[2] - (fixed[2] + 25.0))

    def test_leaf_horizontal_is_butt_joint_and_vertical_is_inserted(self):
        for process in ("v_groove_90:sharp_v", "miter_45", "butt_90"):
            with self.subTest(process=process):
                document = generate(accessDoorEnabled=True, doorLeafFrameJoinType=process)
                items = {item["key"]: item for item in document["items"]}
                horizontal = items["access_door.leaf.horizontal.0001"]["properties"]
                vertical = items["access_door.leaf.vertical.0001"]["properties"]
                self.assertEqual("butt_weld", horizontal["tubeDesigner.jointProcess"]["start"])
                self.assertEqual(0.0, horizontal["tubeDesigner.jointProcess"]["insertionDepth"])
                self.assertEqual(198.0, horizontal["length"])
                self.assertEqual(278.0, vertical["length"])
                self.assertEqual(10.0, vertical["tubeDesigner.jointProcess"]["insertionDepth"])
                # Touching the frame is a butt joint, not a cutter into either stock.
                self.assertFalse(any(".through.access_door.leaf.horizontal." in node["key"]
                                     for node in document["geometry"]))

    def test_continuous_outer_frame_has_all_four_side_piercings(self):
        document = generate(frameLayout="four_sides")
        nodes = graph(document)
        prefix = "outer_frame.continuous.0001.export.through."
        profiles = [node for node in nodes.values()
                    if node["key"].startswith(prefix) and node["operator"] == "profile2d"]
        # 26 physical apertures; the middle bottom aperture is split by the closing seam.
        counts = {side: sum(node["key"].startswith(prefix + side + ".") for node in profiles)
                  for side in ("bottom", "right", "top", "left")}
        self.assertEqual({"bottom": 10, "right": 4, "top": 9, "left": 4}, counts)
        final = nodes["outer_frame.continuous.0001.export.final"]
        for profile in profiles:
            tool = nodes[profile["key"].removesuffix("profile") + "solid"]
            self.assertIn(tool["key"], final["arguments"]["tools"])
            placement = profile["arguments"]["placement"]
            start = placement["origin"][2]
            end = start + tool["arguments"]["vector"][2]
            # Piercing reaches the inside face (+19), preserving the outside wall (-19).
            self.assertLess(min(start, end), 17.8)
            self.assertGreater(max(start, end), 19.0)
            self.assertGreater(min(start, end) - 1.1, -17.8)
            self.assertAlmostEqual(0.0, tool["arguments"]["vector"][0])
            self.assertAlmostEqual(0.0, tool["arguments"]["vector"][1])

    def test_unfolded_holes_fold_back_to_the_assembled_bar_centers(self):
        nodes = graph(generate(frameLayout="four_sides"))
        width, height, wall = 1200.0, 1800.0, 1.2
        h, v = width - 2 * wall, height - 2 * wall
        bend = math.pi / 2 * 0.62 * wall
        length = 2 * h + 2 * v + 4 * bend
        prefix = "outer_frame.continuous.0001.export.through."
        for node in nodes.values():
            if not node["key"].startswith(prefix) or node["operator"] != "profile2d":
                continue
            side, _, _, number, _ = node["key"][len(prefix):].removesuffix(".profile").split(".")
            index = int(number)
            u = node["arguments"]["placement"]["origin"][0]
            if side == "bottom":
                assembled = width / 2 + (u if u < length / 2 else u - length)
            elif side == "top":
                assembled = width - wall - (u - (h / 2 + v + 2 * bend))
            elif side == "right":
                assembled = wall + u - (h / 2 + bend)
            else:
                assembled = height - wall - (u - (1.5 * h + v + 3 * bend))
            expected = (38 + 112.4 * index if side in {"top", "bottom"}
                        else 1600 - (1400 / 3) * (index - 1))
            self.assertAlmostEqual(expected, assembled, places=7, msg=node["key"])

    def test_center_aperture_is_preserved_on_both_ends_of_the_stock(self):
        document = generate(frameLayout="four_sides")
        nodes = graph(document)
        profiles = [node for node in nodes.values()
                    if ".through.bottom.main_grid.vertical.0005." in node["key"]
                    and node["operator"] == "profile2d"]
        self.assertEqual(2, len(profiles))
        origins = sorted(node["arguments"]["placement"]["origin"][0] for node in profiles)
        frame = next(item for item in document["items"] if item["key"].startswith("outer_frame."))
        self.assertAlmostEqual(0.0, origins[0])
        self.assertAlmostEqual(frame["properties"]["length"], origins[1], places=3)
        self.assertEqual(profiles[0]["arguments"]["contours"], profiles[1]["arguments"]["contours"])
        self.assertAlmostEqual(9.6, profiles[0]["arguments"]["contours"][0]["radius"])

    def test_continuous_leaf_has_vertical_piercings_but_fixed_butt_joints_do_not(self):
        document = generate(accessDoorEnabled=True)
        profiles = [node for node in document["geometry"]
                    if ".export.through." in node["key"] and node["operator"] == "profile2d"]
        self.assertEqual(3, len(profiles))  # Bottom seam has two halves, plus the top hole.
        self.assertTrue(all(node["key"].startswith("access_door.leaf.frame.") for node in profiles))

    def test_display_does_not_construct_manufacturing_operators(self):
        original = NeutralModel.geometry

        def checked(model, key, operator, **kwargs):
            self.assertNotEqual("boolean", operator)
            self.assertFalse(any(marker in key for marker in (".through.", ".export.", ".miter.")))
            return original(model, key, operator, **kwargs)

        with patch.object(NeutralModel, "geometry", checked):
            generate("display", frameLayout="four_sides", accessDoorEnabled=True)

    def test_overlapping_bars_and_outside_offsets_are_rejected(self):
        cases = (
            {"horizontalCount": 100},
            {"verticalLayoutMode": "manual_count", "middleVerticalCount": 100},
            {"firstHorizontalTopOffset": 0.0},
            {"lastHorizontalBottomOffset": 0.0},
            {"accessDoorEnabled": True, "doorHorizontalCount": 100},
            {"accessDoorEnabled": True, "doorVerticalCount": 100},
        )
        for case in cases:
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, "范围|重叠"):
                generate(**case)

    def test_insertion_requires_cavity_and_preserves_outside_wall(self):
        cases = (
            {"frameWallThickness": 2.0},
            {"horizontalBranchReserve": 1.0},
            {"accessDoorEnabled": True, "doorLeafFrameWallThickness": 2.0},
        )
        for case in cases:
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, "内腔|入榫"):
                generate(**case)
        # Weld mode is explicit and does not require an insertion cavity.
        generate(frameWallThickness=2.0, frameCornerRadius=1.0,
                 mainHorizontalConnection="weld")

    def test_main_weld_trims_only_outer_ends_and_keeps_vertical_through_holes(self):
        captured = {}
        original = SUBJECT._emit_tube_geometry

        def observe(model, part, shared):
            captured[part.key] = part
            return original(model, part, shared)

        with patch.object(SUBJECT, "_emit_tube_geometry", observe):
            inserted = generate(frameDepth=28.0)
        inserted_parts = dict(captured)
        captured.clear()
        with patch.object(SUBJECT, "_emit_tube_geometry", observe):
            welded = generate(frameDepth=28.0, mainHorizontalConnection="weld")
        for key, part in captured.items():
            if key.startswith("main_grid.horizontal."):
                self.assertEqual((38.0, 1162.0), (part.start[0], part.end[0]))
                self.assertAlmostEqual(20.0, inserted_parts[key].length - part.length)
                self.assertEqual(("square", "square"), (part.start_cut, part.end_cut))
            else:
                self.assertEqual(inserted_parts[key], part)
        for document, expected in ((inserted, 8), (welded, 0)):
            outer_holes = [node for node in document["geometry"] if node["key"].startswith("outer_frame.")
                           and ".through." in node["key"] and node["operator"] == "profile2d"]
            self.assertEqual(expected, len(outer_holes))
        def grid_holes(document):
            return [node for node in document["geometry"] if node["key"].startswith("main_grid.horizontal.")
                    and ".through." in node["key"]]
        self.assertEqual(72, len(grid_holes(welded)))  # 36 actual piercings, profile + extrusion.
        self.assertEqual(grid_holes(inserted), grid_holes(welded))

    def test_weld_does_not_read_hidden_insertion_depth(self):
        for value in (None, float("nan"), -1.0, -100.0, "hidden"):
            with self.subTest(value=value):
                generate(frameDepth=28.0, mainHorizontalConnection="weld", horizontalBranchReserve=value)
        params = parameters(frameDepth=28.0, mainHorizontalConnection="weld")
        del params["horizontalBranchReserve"]
        SUBJECT._generate_geometry(params, {"template": {}, "geometryPurpose": "manufacturing"})

    def test_insert_has_no_implicit_negative_depth_or_zero_depth_weld(self):
        for key in ("horizontalBranchReserve", "verticalBranchReserve"):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "0到20"):
                generate(**{key: -1.0})
        with self.assertRaisesRegex(ValueError, "请选择焊接"):
            generate(horizontalBranchReserve=0.0)
        with self.assertRaisesRegex(ValueError, "mainHorizontalConnection"):
            generate(mainHorizontalConnection="automatic")

    def test_weld_rejects_curved_receiver_or_end_across_rounded_face(self):
        with self.assertRaisesRegex(ValueError, "圆角处留缝"):
            generate(mainHorizontalConnection="weld")  # 22 mm end on a 21 mm straight face.
        with self.assertRaisesRegex(ValueError, "标准矩形外框"):
            generate(mainHorizontalConnection="weld", frameProfileType="round")
        # Exact tangency to the outer-face straight segment is supported.
        generate(mainHorizontalConnection="weld", frameCornerRadius=1.5)
        # A round branch has a planar cut end too; the receiver must remain flat.
        generate(mainHorizontalConnection="weld", frameDepth=28.0, horizontalProfileType="round")

    def test_opening_ends_remain_butt_joints_and_metadata_matches_relationships_and_table(self):
        for connection in ("insert", "weld"):
            with self.subTest(connection=connection):
                document = generate(frameDepth=28.0, accessDoorEnabled=True, doorBottom=600.0,
                                    mainHorizontalConnection=connection)
                items = {item["key"]: item for item in document["items"]}
                joints = [relation for relation in document["relationships"]
                          if relation["key"].startswith("main_grid.horizontal.")]
                main = [item for item in items.values() if item["key"].startswith("main_grid.horizontal.")]
                self.assertEqual(2 * len(main), len(joints))
                fixed_connections = [relation for relation in joints
                                     if any(key.startswith("access_door.fixed_frame.")
                                            for key in relation["items"])]
                self.assertEqual(2, len(fixed_connections))
                for relation in fixed_connections:
                    self.assertEqual("weld", relation["kind"])
                    self.assertEqual(0.0, relation["properties"]["insertionDepth"])
                rows = {row["itemKey"]: row["values"] for row in document["tables"][0]["rows"]}
                for item in main:
                    process = item["properties"]["tubeDesigner.jointProcess"]
                    self.assertEqual(connection, process["mainHorizontalConnection"])
                    self.assertEqual(item["properties"]["length"], rows[item["key"]]["length"])
                    self.assertIn("贴合焊接" if connection == "weld" else "插接", rows[item["key"]]["connection"])
                    for end in ("start", "end"):
                        receiver = process[f"{end}Receiver"]
                        self.assertIn(receiver, items)
                        is_insert = connection == "insert" and receiver.startswith("outer_frame.")
                        self.assertEqual("insert" if is_insert else "butt_weld", process[end])
                        self.assertEqual(10.0 if is_insert else 0.0, process[f"{end}InsertionDepth"])

    def test_connection_modes_cover_all_outer_corner_processes(self):
        processes = ("miter_45", "butt_90", "v_groove_90:sharp_v", "v_groove_90:rounded_v",
                     "v_groove_90:left_arc", "v_groove_90:right_arc")
        for process in processes:
            for wrap in ("side_wraps_horizontal", "horizontal_wraps_side"):
                with self.subTest(process=process, wrap=wrap):
                    options = dict(frameLayout="four_sides", frameDepth=28.0,
                                   frameJoinType=process, frameButtWrapMode=wrap)
                    inserted = generate(**options)
                    welded = generate(**options, mainHorizontalConnection="weld")
                    def count_holes(document):
                        return sum(node["key"].startswith("outer_frame.") and ".through." in node["key"]
                                   and node["operator"] == "profile2d" for node in document["geometry"])
                    self.assertEqual(8, count_holes(inserted) - count_holes(welded))
                    self.assertEqual(19 if process.startswith("v_groove") else 18, count_holes(welded))
                    display = generate("display", **options, mainHorizontalConnection="weld")
                    self.assertEqual([item["properties"] for item in display["items"]],
                                     [item["properties"] for item in welded["items"]])
                    self.assertEqual(display["relationships"], welded["relationships"])
                    self.assertFalse(any(".through." in node["key"] for node in display["geometry"]))

    def test_center_plate_split_preserves_only_actual_outer_end_connections(self):
        document = generate(frameDepth=28.0, mainHorizontalConnection="weld", mainInfillMode="center_plate")
        parts = [item for item in document["items"] if item["key"].startswith("main_grid.horizontal.")]
        split = [item for item in parts if ".panel_" in item["key"]]
        self.assertTrue(split)
        for item in split:
            joints = item["properties"]["tubeDesigner.jointProcess"]
            self.assertEqual("butt_weld", joints["start"])
            self.assertEqual("butt_weld", joints["end"])
            receivers = [joints[f"{end}Receiver"] for end in ("start", "end")]
            self.assertEqual(1, sum(key.startswith("outer_frame.") for key in receivers))
            self.assertEqual(1, sum(key.startswith("center_plate.frame.") for key in receivers))

    def test_too_small_leaf_fails_before_geometry_construction(self):
        with patch.object(NeutralModel, "geometry", side_effect=AssertionError("geometry emitted")):
            with self.assertRaisesRegex(ValueError, "尺寸不足"):
                generate(accessDoorEnabled=True, doorWidth=90.0)

    def test_disabled_opening_does_not_read_or_validate_hidden_door_fields(self):
        params = parameters()
        for key in list(params):
            if key.startswith("door"):
                del params[key]
        document = SUBJECT._generate_geometry(params, {"template": {}, "geometryPurpose": "manufacturing"})
        self.assertEqual(15, len(document["items"]))
        document = generate(doorHingeCount=0, doorHingeSide="unused",
                            doorHorizontalCount=-1, doorFrameJoinType="unused",
                            doorLeft=float("nan"), doorGap=-1.0)
        self.assertEqual(15, len(document["items"]))

    def test_unused_corner_process_does_not_block_two_side_frame(self):
        document = generate(frameJoinType="unused", vGrooveKFactor=float("nan"),
                            vGrooveBottomDistance=-1.0)
        self.assertEqual(15, len(document["items"]))

    def test_active_groove_dimensions_reject_negative_and_nonfinite_values(self):
        cases = (
            {"vGrooveBottomDistance": -1.0},
            {"vGrooveBottomDistance": float("nan")},
            {"vGrooveKFactor": float("inf")},
            {"frameJoinType": "v_groove_90:rounded_v", "vGrooveRadius": -1.0},
            {"frameJoinType": "v_groove_90:rounded_v", "vGrooveRadius": float("nan")},
            {"vGrooveReliefHole": True, "vGrooveReliefDiameter": -1.0},
            {"vGrooveReliefHole": True, "vGrooveReliefDiameter": float("inf")},
        )
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                generate(frameLayout="four_sides", **case)

    def test_single_horizontal_bar_does_not_mask_negative_offsets_by_averaging(self):
        for key in ("firstHorizontalTopOffset", "lastHorizontalBottomOffset"):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "不能为负数"):
                generate(horizontalCount=1, **{key: -100.0})


if __name__ == "__main__":
    unittest.main()
