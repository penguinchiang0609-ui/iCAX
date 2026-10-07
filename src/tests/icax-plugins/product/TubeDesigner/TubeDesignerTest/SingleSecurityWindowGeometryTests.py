"""Single-face frame, insertion and unfolding regressions without OCC or UI."""

from __future__ import annotations

import importlib.util
from copy import deepcopy
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

PACKAGE = ROOT / "src/apps/tube-designer/templates/product/single_face_security_window"
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
                  firstHorizontalTopOffset=200.0, lastHorizontalBottomOffset=200.0,
                  horizontalMaximumCenterSpacing=500.0,
                  verticalLeftCenterOffset=110.0, verticalRightCenterOffset=110.0,
                  verticalMaximumCenterSpacing=120.0,
                  frameWidth=38.0, frameDepth=25.0, frameWallThickness=1.2,
                  horizontalWidth=22.0, horizontalDepth=22.0, horizontalWallThickness=1.0,
                  verticalWidth=19.0, verticalDepth=19.0, verticalWallThickness=1.0,
                  horizontalBranchReserve=10.0, verticalBranchReserve=10.0,
                  frameProfileType="rect",
                  horizontalProfileType="rect", verticalProfileType="round",
                  frameCornerRadius=2.0, horizontalCornerRadius=2.0,
                  verticalCornerRadius=0.0,
                  assemblyClearance=0.1, accessDoorEnabled=False,
                  doorWidth=300.0, doorHeight=360.0, doorLeft=450.0, doorBottom=700.0,
                  doorGap=6.0, doorFrameWidth=25.0, doorFrameDepth=25.0,
                  doorFrameWallThickness=1.0, doorLeafFrameWidth=20.0,
                  doorLeafFrameDepth=20.0, doorLeafFrameWallThickness=1.0,
                  doorHorizontalTopCenterOffset=118.0,
                  doorHorizontalBottomCenterOffset=118.0,
                  doorHorizontalMaximumCenterSpacing=400.0,
                  doorVerticalLeftCenterOffset=99.0,
                  doorVerticalRightCenterOffset=99.0,
                  doorVerticalMaximumCenterSpacing=120.0,
                  doorHorizontalWidth=20.0, doorHorizontalDepth=20.0,
                  doorHorizontalWallThickness=0.8, doorVerticalWidth=16.0,
                  doorVerticalWallThickness=0.8,
                  frameJoinType="v_groove_90:tool_library", doorFrameJoinType="v_groove_90:tool_library",
                  doorLeafFrameJoinType="v_groove_90:tool_library")
    # Manufacturing mode now owns the outer frame; these legacy geometry
    # cases explicitly select the same process they were originally testing.
    result['frameManufacturingMode'] = 'plane_v_notch'
    if 'frameJoinType' in overrides and not overrides['frameJoinType'].startswith('v_groove_90:'):
        result['frameManufacturingMode'] = 'segment_weld'
    result.update(overrides)
    return result


def generate(purpose="manufacturing", **overrides):
    return SUBJECT._generate_geometry(parameters(**overrides), {"template": {}, "geometryPurpose": purpose})


def graph(document):
    return {node["key"]: node for node in document["geometry"]}


def apertures(document, prefix):
    return [record for record in document.get('extensions', {}).get('tubeDesigner.assemblyGeometryProcesses', {}).get('instances', [])
            if record['templateId']=='tube-profile-aperture' and record['stockId'].startswith(prefix)]


def aperture_geometry(record):
    return {node['key']:node for node in record['result']['geometry']}


def dependencies(document, root):
    nodes=graph(document)
    seen=set()
    def visit(key):
        if key in seen: return
        seen.add(key)
        for dependency in nodes[key]['inputs']: visit(dependency)
    visit(root)
    return seen


class SingleSecurityWindowGeometryTests(unittest.TestCase):
    def test_external_allocation_kernel_emits_uncut_stock_and_l_t_nodes(self):
        for pattern in ("horizontal", "vertical"):
            with self.subTest(pattern=pattern):
                values = parameters(assemblyPlanningMode="external_templates",
                                    frameLayout="four_sides", infillPattern=pattern,
                                    accessDoorEnabled=False)
                original = deepcopy(values)
                document = SUBJECT._generate_manufacturing_geometry(values, {"template": {}, "geometryPurpose": "manufacturing"})
                self.assertEqual(original, values)
                self.assertEqual(original, document["parameters"])
                self.assertFalse(any(node["operator"] == "boolean" for node in document["geometry"]))
                items = {item["key"]: item for item in document["items"]}
                self.assertEqual(4 + (4 if pattern == "horizontal" else 9), len(items))
                self.assertTrue(all(item["properties"]["tubeDesigner.assemblyPlanning"] ==
                                    {"stockState": "uncut", "ready": False}
                                    for item in items.values()))
                geometry = graph(document)
                for item in items.values():
                    properties = item["properties"]
                    frame = properties["assemblyFrame.member"]
                    interval = frame["stockInterval"]
                    self.assertLess(interval["startStation"], 0)
                    self.assertGreater(interval["endStation"], frame["axisLength"])
                    self.assertAlmostEqual(properties["length"],
                                           interval["endStation"]-interval["startStation"], places=3)
                    solid = geometry[item["representations"]["result"]]
                    extrusion = geometry[solid["inputs"][0]]
                    self.assertAlmostEqual(math.dist([0,0,0],extrusion["arguments"]["vector"]),
                                           interval["endStation"]-interval["startStation"])
                    section = frame["sectionFrame"]
                    x_axis, y_axis = section["xAxis"], section["yAxis"]
                    axis = properties["tubeDesigner.manufacturingStartToEnd"]
                    cross = [x_axis[1]*y_axis[2]-x_axis[2]*y_axis[1],
                             x_axis[2]*y_axis[0]-x_axis[0]*y_axis[2],
                             x_axis[0]*y_axis[1]-x_axis[1]*y_axis[0]]
                    for coordinate in range(3):
                        self.assertAlmostEqual(cross[coordinate], axis[coordinate])
                        self.assertAlmostEqual(
                            section["originAtStart"][coordinate]
                            + x_axis[coordinate]*section["centerlineUV"][0]
                            + y_axis[coordinate]*section["centerlineUV"][1],
                            frame["start"][coordinate])
                        self.assertAlmostEqual(
                            section["originAtStart"][coordinate]
                            + axis[coordinate]*interval["startStation"],
                            solid["arguments"]["placement"]["origin"][coordinate])
                self.assertEqual(items["outer_frame.left.0001"]["properties"]
                                 ["tubeDesigner.profile"]["contours"],
                                 items["outer_frame.bottom.0001"]["properties"]
                                 ["tubeDesigner.profile"]["contours"])
                nodes = document["relationships"]
                corners = [node for node in nodes if node["properties"]["topology"] == "L"]
                junctions = [node for node in nodes if node["properties"]["topology"] == "T"]
                self.assertEqual(4, len(corners))
                self.assertEqual(2 * (len(items) - 4), len(junctions))
                self.assertTrue(all([anchor["kind"] for anchor in node["properties"]["participantAnchors"]]
                                    == ["end", "end"] for node in corners))
                self.assertTrue(all([anchor["kind"] for anchor in node["properties"]["participantAnchors"]]
                                    == ["side", "end"] for node in junctions))
                for node in nodes:
                    point = node["properties"]["centerlinePoint"]
                    self.assertEqual(node["items"], [anchor["itemKey"]
                                   for anchor in node["properties"]["participantAnchors"]])
                    for anchor in node["properties"]["participantAnchors"]:
                        self.assertEqual(point, anchor["centerlinePoint"])
                        if anchor["kind"] == "end":
                            end = anchor["end"]
                            frame = items[anchor["itemKey"]]["properties"]["assemblyFrame.member"]
                            self.assertEqual(anchor["localAxialStation"],
                                             0.0 if end == "start" else frame["axisLength"])
                            interval = frame["stockInterval"]
                            self.assertAlmostEqual(anchor["anchor"]["stockAllowance"],
                                -interval["startStation"] if end == "start" else
                                interval["endStation"]-frame["axisLength"])
                            if node["properties"]["topology"] == "L":
                                self.assertAlmostEqual(anchor["anchor"]["contactInset"],
                                                       anchor["anchor"]["stockAllowance"])
                        else:
                            station = anchor["localAxialStation"]
                            self.assertGreater(station, 0)
                            self.assertLess(station, items[anchor["itemKey"]]["properties"]["length"])
                            section = (items[anchor["itemKey"]]["properties"]
                                       ["assemblyFrame.member"]["sectionFrame"])
                            self.assertEqual(section["faceNormals"][anchor["face"]],
                                             anchor["faceNormal"])
                self.assertGreaterEqual(sum("outer_frame.left.0001" in node["items"]
                                            for node in nodes), 2)
                self.assertEqual("external_templates", document["extensions"]
                                 ["tubeDesigner.securityWindowReview"]["outerFrameManufacturing"]["mode"])

    def test_external_assembly_does_not_consume_legacy_process_values(self):
        base = parameters(assemblyPlanningMode="external_templates", frameLayout="four_sides",
                          infillPattern="horizontal", accessDoorEnabled=False)
        changed = dict(base, frameManufacturingMode="segment_weld", frameJoinType="butt_90",
                       frameButtWrapMode="horizontal_wraps_side", horizontalBranchReserve=0,
                       verticalBranchReserve=0, assemblyClearance=100)
        expected = SUBJECT.generate(base, {"template": {}, "geometryPurpose": "manufacturing"})
        actual = SUBJECT.generate(changed, {"template": {}, "geometryPurpose": "manufacturing"})
        self.assertEqual(expected["geometry"], actual["geometry"])
        self.assertEqual(expected["items"], actual["items"])
        self.assertEqual(expected["relationships"], actual["relationships"])
        self.assertEqual(changed, actual["parameters"])

    def test_external_supplied_offset_rectangle_keeps_profile_identity(self):
        def rectangle(width, depth):
            points = [[-width/2, 3-depth/2], [width/2, 3-depth/2],
                      [width/2, 3+depth/2], [-width/2, 3+depth/2]]
            return {"kind": "path", "closed": True, "segments": [
                {"kind": "line", "start": points[i], "end": points[(i+1) % 4]}
                for i in range(4)]}
        contours = [rectangle(38, 25), rectangle(35.6, 22.6)]
        profile = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                   "kind": "fixed-section", "profileForm": "fixed",
                   "sectionKind": "rect", "name": "偏置矩形管", "specification": "38×25×1.2",
                   "contentDigest": "offset-rect-test", "width": 38, "depth": 25,
                   "wallThickness": 1.2, "geometrySource": "providedBoundary",
                   "contours": contours}
        values = parameters(assemblyPlanningMode="external_templates",
                            frameLayout="four_sides", infillPattern="horizontal",
                            accessDoorEnabled=False,
                            tubeDesignerProfileOverrides={"frame": profile})
        original = deepcopy(values)
        document = SUBJECT.generate(values, {"template": {}, "geometryPurpose": "display"})
        self.assertEqual(values, original)
        self.assertEqual(document["parameters"], original)
        frames = {item["key"]: item["properties"] for item in document["items"]
                  if item["key"].startswith("outer_frame.")}
        self.assertTrue(all(item["tubeDesigner.profile"]["contours"] == contours
                            for item in frames.values()))
        self.assertEqual(frames["outer_frame.bottom.0001"]["assemblyFrame.member"]
                         ["sectionFrame"]["centerlineUV"], [0.0, 6.0])
        self.assertEqual(frames["outer_frame.top.0001"]["assemblyFrame.member"]
                         ["sectionFrame"]["centerlineUV"], [0.0, 6.0])
        stock = SUBJECT.generate(values, {"template": {}, "geometryPurpose": "manufacturing"})
        for item in stock['items']:
            if item['key'] not in frames:
                continue
            span=item['properties']['manufacturing.sourceMembers'][0]['spans'][0]
            placement=span['placement']
            section=frames[item['key']]['assemblyFrame.member']['sectionFrame']
            origin=section['originAtStart']
            mapped=[sum(origin[i]*placement[axis][j]
                        for i,axis in enumerate(('xAxis','yAxis','zAxis')))+placement['origin'][j]
                    for j in range(3)]
            self.assertAlmostEqual(span['stockStart'], mapped[0], places=7)
            self.assertAlmostEqual(0.0, mapped[1], places=7)
            self.assertAlmostEqual(0.0, mapped[2], places=7)

    def test_external_assembly_rejects_unimplemented_product_topologies(self):
        cases = (
            ({"faceType": "two"}, "单面"),
            ({"frameLayout": "left_right"}, "四边框"),
            ({"accessDoorEnabled": True}, "开启口"),
            ({"infillPattern": "grid"}, "交叉节点"),
        )
        for overrides, message in cases:
            with self.subTest(overrides=overrides), self.assertRaisesRegex(ValueError, message):
                values = parameters(assemblyPlanningMode="external_templates",
                                    frameLayout="four_sides", infillPattern="horizontal",
                                    accessDoorEnabled=False)
                values.update(overrides)
                SUBJECT.generate(values, {"template": {}})

    def test_inactive_infill_profiles_never_load_or_change_geometry(self):
        descriptor = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
        defaults = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
        real_profile = SUBJECT._profile
        for pattern, inactive in (("vertical", ("horizontal", "doorHorizontal")),
                                  ("horizontal", ("vertical", "doorVertical"))):
            for opening in (False, True):
                for purpose in ("display", "manufacturing"):
                    with self.subTest(pattern=pattern, opening=opening, purpose=purpose):
                        values = dict(defaults, infillPattern=pattern, accessDoorEnabled=opening)
                        context = {"template": descriptor, "geometryPurpose": purpose}
                        expected = SUBJECT.generate(deepcopy(values), context)
                        for prefix in inactive:
                            for key in list(values):
                                if key.startswith(prefix):
                                    values[key] = "inactive" if isinstance(values[key], str) else -1
                        original = deepcopy(values)
                        def active_profile(parameters, prefix):
                            self.assertNotIn(prefix, inactive)
                            return real_profile(parameters, prefix)
                        with patch.object(SUBJECT, "_profile", side_effect=active_profile):
                            actual = SUBJECT.generate(values, context)
                        self.assertEqual(values, original)
                        self.assertEqual(actual["parameters"], original)
                        self.assertEqual(actual["geometry"], expected["geometry"])
                        self.assertEqual(actual["items"], expected["items"])

    def test_active_crossing_profiles_still_require_through_clearance(self):
        with self.assertRaisesRegex(ValueError, "无法安全穿管"):
            generate(horizontalWallThickness=2)

    def test_public_style_generates_only_tube_grid(self):
        descriptor = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
        self.assertNotIn("presets", descriptor["extensions"]["catalog"])
        self.assertFalse(any(p["key"].startswith("centerPlate") or p["key"] == "mainInfillMode"
                             for p in descriptor["parameters"]))
        defaults = {p["key"]: p["defaultValue"] for p in descriptor["parameters"]}
        for purpose in ("display", "manufacturing"):
            document = SUBJECT.generate(defaults, {"template": {}, "geometryPurpose": purpose})
            self.assertTrue(document["items"])
            self.assertFalse(any("center_plate" in item["key"] for item in document["items"]))

    def test_default_spacing_and_existing_side_frame_piercings(self):
        document = generate()
        self.assertEqual(15, len(document["items"]))
        self.assertEqual(8, len(apertures(document,'outer_frame.')))
        step = (1200.0 - 2 * 38.0 - 110.0 - 110.0) / 8
        self.assertAlmostEqual(103.5, step - 19.0 / 2)
        self.assertAlmostEqual(94.0, step - 19.0)

    def test_gap_is_between_actual_frame_and_leaf_surfaces(self):
        for gap in (0.0, 2.0, 6.0):
            with self.subTest(gap=gap):
                inset = 25.0 + gap + 20.0
                nodes = graph(generate(
                    "display", accessDoorEnabled=True, doorGap=gap,
                    doorHorizontalTopCenterOffset=(360.0 - 2 * inset) / 2,
                    doorHorizontalBottomCenterOffset=(360.0 - 2 * inset) / 2,
                    doorVerticalLeftCenterOffset=(300.0 - 2 * inset) / 2,
                    doorVerticalRightCenterOffset=(300.0 - 2 * inset) / 2,
                ))
                fixed = nodes["access_door.fixed_frame.continuous.0001.display.left.solid"]["arguments"]["placement"]["origin"]
                leaf = nodes["access_door.leaf.frame.continuous.0001.display.left.solid"]["arguments"]["placement"]["origin"]
                self.assertAlmostEqual(gap, (leaf[0] - 10.0) - (fixed[0] + 12.5))
                self.assertAlmostEqual(gap, leaf[2] - (fixed[2] + 25.0))

    def test_escape_grid_uses_center_spacing_and_is_independent_of_tube_diameter(self):
        options = dict(
            accessDoorEnabled=True,
            doorVerticalLeftCenterOffset=20.0,
            doorVerticalRightCenterOffset=20.0,
            doorVerticalMaximumCenterSpacing=60.0,
        )
        narrow = generate("display", **options, doorVerticalWidth=16.0, doorVerticalDepth=16.0)
        wide = generate("display", **options, doorVerticalWidth=17.0, doorVerticalDepth=17.0)
        keys = lambda document: [item["key"] for item in document["items"]
                                 if item["key"].startswith("access_door.leaf.vertical.")]
        self.assertEqual(4, len(keys(narrow)))
        self.assertEqual(keys(narrow), keys(wide))
        origins = lambda document: [node["arguments"]["placement"]["origin"]
                                    for node in document["geometry"]
                                    if node["key"].startswith("access_door.leaf.vertical.")
                                    and node["key"].endswith(".solid")]
        self.assertEqual(origins(narrow), origins(wide))

    def test_leaf_horizontal_is_butt_joint_and_vertical_is_inserted(self):
        for process in ("v_groove_90:tool_library", "miter_45", "butt_90"):
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
        records=apertures(document,'outer_frame.continuous.0001')
        prefix = "outer_frame.continuous.0001.aperture."
        # 26 physical apertures; the middle bottom aperture is split by the closing seam.
        counts = {side: sum(record['instanceId'].startswith(prefix + side + ".") for record in records)
                  for side in ("bottom", "right", "top", "left")}
        self.assertEqual({"bottom": 10, "right": 4, "top": 9, "left": 4}, counts)
        frame=next(i for i in document['items'] if i['key']=='outer_frame.continuous.0001')
        final=dependencies(document,frame['representations']['result'])
        for record in records:
            nodes=aperture_geometry(record)
            profile,tool=nodes['aperture.profile'],nodes['aperture.solid']
            self.assertIn(record['resultGeometry'],final)
            placement = profile["arguments"]["placement"]
            start = placement["origin"][2]
            end = start + tool["arguments"]["vector"][2]
            # Piercing reaches the inside face (+19), preserving the outside wall (-19).
            self.assertLess(min(start, end), 17.8)
            self.assertGreater(max(start, end), 19.0)
            self.assertGreater(min(start, end) - tool['arguments']['extendStart'], -17.8)
            cut=record['result']['checks'][0]['bounds']
            self.assertGreater(cut['min'][2],-17.8)
            self.assertAlmostEqual(19.0,cut['max'][2])
            self.assertAlmostEqual(0.0, tool["arguments"]["vector"][0])
            self.assertAlmostEqual(0.0, tool["arguments"]["vector"][1])

    def test_unfolded_holes_fold_back_to_the_assembled_bar_centers(self):
        document = generate(frameLayout="four_sides")
        frame = next(item for item in document['items']
                     if item['key'] == 'outer_frame.continuous.0001')
        spans = frame['properties']['tubeDesigner.sourceSpans']
        prefix = "outer_frame.continuous.0001.aperture."
        checked = 0
        for record in apertures(document,'outer_frame.continuous.0001'):
            node=aperture_geometry(record)['aperture.profile']
            side, _, _, number, _ = record['instanceId'][len(prefix):].split('.')
            index = int(number)
            point = node['arguments']['placement']['origin']
            matched = []
            for span in spans:
                if span['itemKey'] != 'outer_frame.' + side + '.0001':
                    continue
                low = span['stockStart']
                high = low + span['startReserve'] + math.dist(span['start'], span['end']) + span['endReserve']
                if not low-1e-7 <= point[0] <= high+1e-7:
                    continue
                placement = span['placement']
                axes = [placement[name] for name in ('xAxis','yAxis','zAxis')]
                world = [sum((point[j]-placement['origin'][j])*axes[i][j] for j in range(3))
                         for i in range(3)]
                matched.append(world)
            self.assertTrue(matched, record['instanceId'])
            expected = (148 + 113*(index-1) if side in {'top','bottom'}
                        else 200 + (1400/3)*(index-1))
            for world in matched:
                assembled = world[0] if side in {'top','bottom'} else world[2]
                self.assertAlmostEqual(expected, assembled, places=7, msg=record['instanceId'])
            checked += 1
        self.assertEqual(27, checked)  # one physical aperture is split at the seam

    def test_center_aperture_is_preserved_on_both_ends_of_the_stock(self):
        document = generate(frameLayout="four_sides")
        profiles = [aperture_geometry(record)['aperture.profile'] for record in apertures(document,'outer_frame.')
                    if '.aperture.bottom.main_grid.vertical.0005.' in record['instanceId']]
        self.assertEqual(2, len(profiles))
        origins = sorted(node["arguments"]["placement"]["origin"][0] for node in profiles)
        frame = next(item for item in document["items"] if item["key"].startswith("outer_frame."))
        self.assertAlmostEqual(0.0, origins[0])
        self.assertAlmostEqual(frame["properties"]["length"], origins[1], places=3)
        self.assertEqual(profiles[0]["arguments"]["contours"], profiles[1]["arguments"]["contours"])
        contour = profiles[0]["arguments"]["contours"][0]
        self.assertEqual(contour["kind"], "path")
        self.assertTrue(contour["closed"])
        self.assertEqual(len(contour["segments"]), 4)
        for segment in contour["segments"]:
            self.assertEqual(segment["kind"], "arc")
            for point in (segment["start"], segment["middle"], segment["end"]):
                self.assertAlmostEqual(9.6, math.hypot(*point))

    def test_continuous_leaf_has_vertical_piercings_but_fixed_butt_joints_do_not(self):
        document = generate(accessDoorEnabled=True)
        records=apertures(document,('access_door.fixed_frame.','access_door.leaf.frame.'))
        self.assertEqual(3, len(records))  # Bottom seam has two halves, plus the top hole.
        self.assertTrue(all(record['stockId'].startswith('access_door.leaf.frame.') for record in records))

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
            {"horizontalMaximumCenterSpacing": 1.0},
            {"verticalMaximumCenterSpacing": 1.0},
            {"firstHorizontalTopOffset": 0.0},
            {"lastHorizontalBottomOffset": 0.0},
            {"accessDoorEnabled": True, "doorHorizontalTopCenterOffset": 10.0,
             "doorHorizontalBottomCenterOffset": 10.0, "doorHorizontalMaximumCenterSpacing": 1.0},
            {"accessDoorEnabled": True, "doorVerticalLeftCenterOffset": 10.0,
             "doorVerticalRightCenterOffset": 10.0, "doorVerticalMaximumCenterSpacing": 1.0},
        )
        for case in cases:
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, "范围|重叠|100根"):
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

    def test_insert_requires_positive_depth(self):
        for key in ("horizontalBranchReserve", "verticalBranchReserve"):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "0到20"):
                generate(**{key: -1.0})
        with self.assertRaisesRegex(ValueError, "必须大于0"):
            generate(horizontalBranchReserve=0.0)

    def test_opening_ends_remain_butt_joints_and_metadata_matches_relationships_and_table(self):
        document = generate(frameDepth=28.0, accessDoorEnabled=True, doorBottom=600.0)
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
            self.assertNotIn("mainHorizontalConnection", process)
            self.assertEqual(item["properties"]["length"], rows[item["key"]]["length"])
            for end in ("start", "end"):
                receiver = process[f"{end}Receiver"]
                self.assertIn(receiver, items)
                is_insert = receiver.startswith("outer_frame.")
                self.assertEqual("insert" if is_insert else "butt_weld", process[end])
                self.assertEqual(10.0 if is_insert else 0.0, process[f"{end}InsertionDepth"])
            self.assertIn("插接" if "insert" in (process["start"], process["end"])
                          else "贴合焊接", rows[item["key"]]["connection"])

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
                            doorHorizontalMaximumCenterSpacing=-1, doorFrameJoinType="unused",
                            doorLeft=float("nan"), doorGap=-1.0)
        self.assertEqual(15, len(document["items"]))

    def test_unused_corner_process_does_not_block_two_side_frame(self):
        document = generate(frameJoinType="unused")
        self.assertEqual(15, len(document["items"]))

    def test_removed_product_local_groove_drafts_cannot_override_the_library_process(self):
        expected=generate(frameLayout="four_sides")
        for style in ("sharp_v", "rounded_v", "left_arc", "right_arc"):
            with self.subTest(style=style):
                actual=generate(frameLayout="four_sides", frameJoinType=f"v_groove_90:{style}")
                self.assertEqual(expected['geometry'],actual['geometry'])

    def test_single_horizontal_bar_does_not_mask_negative_offsets_by_averaging(self):
        for key in ("firstHorizontalTopOffset", "lastHorizontalBottomOffset"):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "不能为负数"):
                generate(**{key: -100.0})


if __name__ == "__main__":
    unittest.main()
