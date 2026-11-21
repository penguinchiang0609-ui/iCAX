"""Physical-stock regression for the independent 2.0 grille product.

These tests inspect the actual display extrusion resources as well as layout
contacts. Native Boolean execution and standalone acceptance are separate.
"""
from copy import deepcopy
from itertools import combinations, product
import json
import math
from pathlib import Path
import sys
import unittest


SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template, _scene_specification_annotations
from icax_template_sdk.display import expand_display_model
from icax_template_sdk.manufacturing import validate_manufacturing_model


DIRECTORY = SRC / "apps/tube-designer/templates/product/minimal_protective_grille"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {field["key"]: field["defaultValue"] for field in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "minimal-grille-product-v2-tests")
TOLERANCE = 1.e-7


def placed(point, frame):
    origin = frame.get("origin", [0., 0., 0.])
    axes = [frame.get("xAxis", [1., 0., 0.]), frame.get("yAxis", [0., 1., 0.])]
    a, b = axes
    axes.append(frame.get("zAxis", [a[1]*b[2]-a[2]*b[1],
                                   a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]))
    return tuple(origin[i] + sum(point[j]*axes[j][i] for j in range(3)) for i in range(3))


def displayed_stocks(document):
    """Read stock endpoints and envelopes from resource geometry, not facts."""
    resources = {entry["key"]: entry for entry in document["resources"]}
    stocks = []
    for item in document["items"]:
        instance = item["geometry"]
        extrusion = resources[instance["resource"]]
        if extrusion["operator"] != "extrude":
            raise AssertionError("The displayed stock must be an uncut extrusion")
        section = resources[extrusion["inputs"][0]]
        if section["operator"] != "profile2d":
            raise AssertionError("The displayed extrusion must retain its actual section")
        contours = section["arguments"]["contours"]
        profile = item["properties"]["tubeDesigner.profile"]
        if profile["kind"] == "rect-bar":
            if (profile["hollow"] is not False or profile["wallThickness"] != 0
                    or len(contours) != 1):
                raise AssertionError("Solid rectangular stock must have one actual outer contour and no hollow wall")
        elif profile["kind"] in ("rect", "round"):
            if (profile["hollow"] is not True or profile["wallThickness"] <= 0
                    or len(contours) != 2):
                raise AssertionError("Hollow tube stock must retain its actual outer and inner contours")
        else:
            raise AssertionError("Unexpected grille stock profile: " + profile["kind"])
        if not all(c["closed"] for c in contours):
            raise AssertionError("Every stock section boundary must be closed")
        frame = instance["placement"]
        profile_frame = section["arguments"].get("placement", {})
        vector = extrusion["arguments"]["vector"]
        vertices = [placed([*segment[point], 0.], profile_frame)
                    for segment in contours[0]["segments"]
                    for point in ("start", "middle", "end") if point in segment]
        points = [placed([vertex[i]+end*vector[i] for i in range(3)], frame)
                  for vertex in vertices for end in (0, 1)]
        stocks.append({"key": item["key"], "item": item,
                       "start": placed([0., 0., 0.], frame), "end": placed(vector, frame),
                       "bounds": tuple((min(p[i] for p in points), max(p[i] for p in points))
                                       for i in range(3))})
    return stocks


def overlaps(first, second):
    return all(min(a[1], b[1])-max(a[0], b[0]) > TOLERANCE
               for a, b in zip(first, second))


class MinimalProtectiveGrilleProductTests(unittest.TestCase):
    def values(self, **changes):
        return {**DEFAULTS, **changes}

    def display(self, parameters):
        original = deepcopy(parameters)
        result = MODULE.display(parameters)
        self.assertEqual(parameters, original, "Display must not mutate the host inputs")
        expand_display_model(result)
        self.assertNotIn("parameters", result)
        return result

    def layout(self, parameters):
        original = deepcopy(parameters)
        result = MODULE.layout(parameters)
        self.assertEqual(parameters, original, "Layout must not normalize inputs in place")
        return result

    def manufacturing(self, parameters):
        original = deepcopy(parameters)
        result = MODULE.manufacturing(parameters)
        self.assertEqual(parameters, original, "Manufacturing must not mutate the host inputs")
        validate_manufacturing_model(result)
        self.assertNotIn("parameters", result)
        return result

    def assert_clear_stocks(self, parameters, stocks, openings):
        self.assertEqual(len({stock["key"] for stock in stocks}), len(stocks))
        self.assertTrue(all(math.dist(stock["start"], stock["end"]) > TOLERANCE for stock in stocks))
        for stock in stocks:
            (x0, x1), _, (z0, z1) = stock["bounds"]
            self.assertGreaterEqual(x0, -TOLERANCE, stock["key"])
            self.assertLessEqual(x1, parameters["width"]+TOLERANCE, stock["key"])
            self.assertGreaterEqual(z0, -TOLERANCE, stock["key"])
            self.assertLessEqual(z1, parameters["height"]+TOLERANCE, stock["key"])
            for opening in openings:
                box = ((opening["left"], opening["right"]), (-1000., 1000.),
                       (opening["bottom"], opening["top"]))
                self.assertFalse(overlaps(stock["bounds"], box),
                                 f"{stock['key']} intrudes into a real handle opening")
        for first, second in combinations(stocks, 2):
            self.assertFalse(overlaps(first["bounds"], second["bounds"]),
                             f"{first['key']} and {second['key']} have intersecting stock envelopes")

    def test_matrix_actual_display_has_local_clear_openings_and_no_stock_overlap(self):
        for frame, handle, closure, mode in product(
                ("two_vertical", "closed_frame", "bottom_rail"),
                ("none", "left", "right", "center_double"),
                ("closed", "open_slot"), ("maximum_gap", "fixed_count")):
            with self.subTest(frame=frame, handle=handle, closure=closure, mode=mode):
                parameters = self.values(frameType=frame, handleEnabled="no" if handle == "none" else "yes",
                                         handleSide="left" if handle == "none" else handle,
                                         handleClosure=closure, barLayoutMode=mode)
                layout = self.layout(parameters)
                stocks = displayed_stocks(self.display(parameters))
                expected = []
                fw, bw = parameters["frameWidth"], parameters["innerWidth"]
                if handle in ("left", "right"):
                    left = fw if handle == "left" else parameters["width"]-fw-parameters["handleWidth"]
                    expected.append(dict(left=left, right=left+parameters["handleWidth"],
                                         bottom=475., top=675.))
                elif handle == "center_double":
                    center = parameters["width"]/2
                    expected = [dict(left=center-bw/2-200, right=center-bw/2, bottom=475., top=675.),
                                dict(left=center+bw/2, right=center+bw/2+200, bottom=475., top=675.)]
                self.assertEqual(layout["facts"]["clearOpenings"], expected)
                self.assertEqual(len(stocks), len(layout["members"]))
                self.assert_clear_stocks(parameters, stocks, expected)
                emitted = {stock["key"]: stock for stock in stocks}
                for member in layout["members"]:
                    self.assertEqual(emitted[member["key"]]["start"], member["start"])
                    self.assertEqual(emitted[member["key"]]["end"], member["end"])
                # Single-side openings must not chop upper/lower rows into
                # semantic left/middle/right stock fragments.
                if handle != "center_double":
                    for stock in stocks:
                        if stock["key"].startswith("inner.") and "middle" not in stock["key"] \
                                and stock["start"][2] == stock["end"][2]:
                            self.assertEqual((stock["start"][0], stock["end"][0]),
                                             (fw, parameters["width"]-fw))

    def test_open_slot_splits_only_the_actual_upright_at_the_clear_opening(self):
        for frame, side in product(("two_vertical", "closed_frame", "bottom_rail"),
                                   ("left", "right", "center_double")):
            with self.subTest(frame=frame, side=side):
                parameters = self.values(frameType=frame, handleSide=side)
                closed = displayed_stocks(self.display(parameters))
                opened = displayed_stocks(self.display({**parameters, "handleClosure": "open_slot"}))
                affected = "inner.center." if side == "center_double" else f"frame.{side}."
                closed_affected = [s for s in closed if s["key"].startswith(affected)]
                opened_affected = [s for s in opened if s["key"].startswith(affected)]
                self.assertEqual(len(closed_affected), 1)
                self.assertEqual(len(opened_affected), 2)
                self.assertEqual(opened_affected[0]["start"], closed_affected[0]["start"])
                self.assertEqual(opened_affected[-1]["end"], closed_affected[0]["end"])
                self.assertEqual(opened_affected[0]["end"][2], 475)
                self.assertEqual(opened_affected[1]["start"][2], 675)
                independent = lambda stocks: [(s["key"], s["start"], s["end"], s["bounds"])
                                               for s in stocks if not s["key"].startswith(affected)]
                self.assertEqual(independent(closed), independent(opened))

    def test_fixed_count_includes_first_last_rows_but_not_handle_boundary_rails(self):
        for frame in ("two_vertical", "closed_frame", "bottom_rail"):
            with self.subTest(frame=frame):
                parameters = self.values(frameType=frame, barLayoutMode="fixed_count",
                                         firstBarTopOffset=67, lastBarBottomOffset=73)
                rows = self.layout(parameters)["facts"]["rowCenters"]
                self.assertEqual(len(rows["upper"]), 4)
                self.assertEqual(len(rows["lower"]), 3)
                self.assertEqual(len(rows["middle"]), 1)
                self.assertEqual(rows["lower_boundary"], [470.])
                self.assertEqual(rows["upper_boundary"], [680.])
                if frame != "closed_frame":
                    self.assertEqual(rows["upper"][-1], 1250-67)
                if frame == "two_vertical":
                    self.assertEqual(rows["lower"][0], 73)
                plain = self.layout({**parameters, "handleEnabled": "no", "barCount": 8})
                self.assertEqual(len(plain["facts"]["rowCenters"]["full"]), 8)

    def test_maximum_clear_gap_is_respected_in_every_active_row_zone(self):
        for frame, gap in product(("two_vertical", "closed_frame", "bottom_rail"), (70., 110., 150.)):
            with self.subTest(frame=frame, gap=gap):
                parameters = self.values(frameType=frame, barLayoutMode='maximum_gap', maximumClearGap=gap)
                rows = self.layout(parameters)["facts"]["rowCenters"]
                low = 0 if frame == "two_vertical" else 10
                high = 1240 if frame == "closed_frame" else 1250
                for name, a, b in (("lower", low, 465), ("middle", 475, 675), ("upper", 685, high)):
                    edges = [(a, a), *[(z-5, z+5) for z in rows[name]], (b, b)]
                    for first, second in zip(edges, edges[1:]):
                        self.assertLessEqual(second[0]-first[1], gap+TOLERANCE, (frame, name))

    def test_disabled_and_unrelated_drafts_do_not_change_the_display(self):
        cases = [
            ({"handleEnabled": "no"}, {"handleSide": "inactive", "handleClosure": "inactive",
                "handleBottom": -999, "handleHeight": -999, "handleWidth": -999,
                "handleCenterOffset": 99999, "upperBarCount": -5, "lowerBarCount": -6, "middleBarCount": -7}),
            ({}, {"upperBarCount": -5, "lowerBarCount": -6, "middleBarCount": -7, "barCount": -8}),
            ({"barLayoutMode": "fixed_count"}, {"maximumClearGap": -999}),
            ({"handleSide": "left"}, {"handleCenterOffset": 99999}),
            ({"frameType": "closed_frame"}, {"firstBarTopOffset": -999, "lastBarBottomOffset": -999}),
            ({"frameType": "bottom_rail"}, {"lastBarBottomOffset": -999}),
            ({}, {"jointMode": "male_female_half_hole", "maleHeadLength": -999,
                  "maleHeadWidth": -999, "maleCornerType": "inactive", "maleCornerSize": -999}),
        ]
        for active, inactive in cases:
            with self.subTest(active=active, inactive=inactive):
                baseline = self.values(**active)
                self.assertEqual(self.display(baseline), self.display({**baseline, **inactive}))

    def test_invalid_active_geometry_cannot_return_plausible_stocks(self):
        cases = [
            {"width": 99}, {"height": 99}, {"width": True}, {"width": float("nan")},
            {"height": float("inf")}, {"frameType": "unknown"}, {"handleEnabled": True},
            {"handleSide": "unknown"}, {"handleClosure": "unknown"}, {"barLayoutMode": "unknown"},
            {"handleBottom": 0}, {"handleBottom": 1100}, {"handleWidth": 548},
            {"handleHeight": -1}, {"handleSide": "center_double", "handleCenterOffset": 100},
            {"firstBarTopOffset": 4}, {"lastBarBottomOffset": 4},
            {"firstBarTopOffset": 1200}, {"lastBarBottomOffset": 600},
            {"barLayoutMode": "maximum_gap", "maximumClearGap": 0},
            {"barCenterSpacing": 0}, {"barLayoutMode": "fixed_count", "lowerBarCount": 1.5},
            {"barLayoutMode": "fixed_count", "lowerBarCount": 0},
            {"barLayoutMode": "fixed_count", "upperBarCount": 100},
            {"handleEnabled": "no", "barLayoutMode": "fixed_count", "barCount": 1},
        ]
        for changes in cases:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.display(self.values(**changes))

    def test_horizontal_position_changes_real_openings_without_changing_frame_structure(self):
        base = self.values(handleSide="center_double")
        initial = self.layout(base)
        moved = self.layout({**base, "handleCenterOffset": 25})
        for before, after in zip(initial["facts"]["clearOpenings"], moved["facts"]["clearOpenings"]):
            self.assertEqual(after["left"]-before["left"], 25)
            self.assertEqual(after["right"]-before["right"], 25)
            self.assertEqual((after["bottom"], after["top"]), (before["bottom"], before["top"]))
        frames = lambda state: [(m["key"], m["start"], m["end"]) for m in state["members"]
                                if m["role"].startswith("frame.")]
        self.assertEqual(frames(initial), frames(moved))

    def test_host_annotation_enrichment_preserves_manual_choices_and_physical_defaults(self):
        parameters = self.values()
        original = deepcopy(parameters)
        document = self.display(parameters)
        _scene_specification_annotations(document, DESCRIPTOR, parameters)
        expand_display_model(document)
        self.assertEqual(parameters, original)
        annotations = {a["parameter"]: a for a in document["annotations"]}
        self.assertEqual(len(annotations), len(document["annotations"]))
        for key in ("handleEnabled", "handleSide", "handleClosure", "barLayoutMode"):
            self.assertEqual(annotations[key]["kind"], "parameter")
            self.assertEqual(annotations[key]["generatedValue"], parameters[key])
        for key in ("width", "height", "handleBottom", "handleWidth", "handleHeight",
                    "firstBarTopOffset", "lastBarBottomOffset", "maximumClearGap"):
            self.assertEqual(annotations[key]["generatedValue"], parameters[key])
        self.assertEqual(MODULE.TEMPLATE_ID, DESCRIPTOR["id"])
        self.assertEqual(MODULE.TEMPLATE_VERSION, DESCRIPTOR["version"])
        dependencies = DESCRIPTOR["extensions"]["parameterDependencies"]
        self.assertEqual(dependencies["creationOnly"], ["layoutStyle", "frameType", "supportStyle"])
        self.assertTrue(all(key not in dependencies["manufacturingOnly"] for key in
                            ("handleEnabled", "handleSide", "handleClosure", "handleBottom", "barLayoutMode")))

    def test_default_manufacturing_prepares_one_stock_per_displayed_member(self):
        for frame, handle, closure in product(("two_vertical", "closed_frame", "bottom_rail"),
                ("none", "left", "right", "center_double"), ("closed", "open_slot")):
            with self.subTest(frame=frame, handle=handle, closure=closure):
                parameters = self.values(frameType=frame, handleEnabled="no" if handle == "none" else "yes",
                                         handleSide="left" if handle == "none" else handle,
                                         handleClosure=closure)
                stocks = displayed_stocks(self.display(parameters))
                declaration = self.manufacturing(parameters)
                processes = declaration["processes"]
                self.assertEqual(len(processes), len(stocks))
                self.assertTrue(all(p["definition"]["templateId"] == "profile-stock-preparation" for p in processes))
                by_key = {p["definition"]["targets"]["stock"]: p["definition"] for p in processes}
                self.assertEqual(set(by_key), {stock["key"] for stock in stocks})
                for stock in stocks:
                    interval = by_key[stock["key"]]["processInput"]["geometry"]["axialInterval"]
                    self.assertEqual(interval["startStation"], 0)
                    self.assertAlmostEqual(interval["endStation"], math.dist(stock["start"], stock["end"]))
                self.assertTrue(all(c["kind"] == "weld" and c["properties"]["recipe"] == "butt_weld"
                                    for c in declaration["connections"]))

    def test_first_last_offsets_dimension_real_horizontal_row_centers(self):
        keys = {"firstBarTopOffset", "lastBarBottomOffset"}
        for frame, handle, mode, offsets, frame_width in product(
                ("two_vertical", "closed_frame", "bottom_rail"),
                ("none", "left", "right", "center_double"),
                ("maximum_gap", "fixed_count"), ((25, 35), (80, 110)), (10, 40)):
            with self.subTest(frame=frame, handle=handle, mode=mode,
                              offsets=offsets, frame_width=frame_width):
                parameters = self.values(frameType=frame, frameWidth=frame_width,
                    handleEnabled="no" if handle == "none" else "yes",
                    handleSide="left" if handle == "none" else handle, barLayoutMode=mode,
                    firstBarTopOffset=offsets[0], lastBarBottomOffset=offsets[1])
                document = self.display(parameters)
                layout = self.layout(parameters)
                annotations = {a["parameter"]: a for a in document["annotations"]}
                expected = ({"firstBarTopOffset"} if frame != "closed_frame" else set())
                if frame == "two_vertical":
                    expected.add("lastBarBottomOffset")
                self.assertEqual(set(annotations) & keys, expected)
                row_centers = [z for group in layout["facts"]["rowCenters"].values() for z in group]
                horizontal = [s for s in displayed_stocks(document) if s["key"].startswith("inner.")
                              and abs(s["start"][2]-s["end"][2]) <= TOLERANCE]
                for key in expected:
                    first = key == "firstBarTopOffset"
                    center = max(row_centers) if first else min(row_centers)
                    measured = max(s["end"][2] for s in horizontal) if first else min(s["end"][2] for s in horizontal)
                    self.assertAlmostEqual(center, measured)
                    endpoint = max((s["end"] for s in horizontal
                                    if abs(s["end"][2]-center) <= TOLERANCE), key=lambda point: point[0])
                    annotation = annotations[key]
                    self.assertEqual(annotation["kind"], "linear")
                    self.assertEqual(annotation["end"], list(endpoint))
                    self.assertEqual(annotation["start"], [endpoint[0], endpoint[1],
                                                           float(parameters["height"]) if first else 0.])
                    self.assertAlmostEqual(math.dist(annotation["start"], annotation["end"]), parameters[key])
                    self.assertEqual(annotation["generatedValue"], parameters[key])
                    self.assertEqual(annotation["offset"][1:], [0., 0.])
                    self.assertAlmostEqual(endpoint[0]+annotation["offset"][0], parameters["width"]+75.)
                anchored = deepcopy({key: annotations[key] for key in expected})
                _scene_specification_annotations(document, DESCRIPTOR, parameters)
                enriched = {a["parameter"]: a for a in document["annotations"]}
                for key in expected:
                    self.assertEqual(enriched[key], anchored[key], "Host must preserve the actual stock anchors")

    def test_first_last_offset_visibility_follows_planar_frame_and_active_pitch_anchor(self):
        display = json.loads((DIRECTORY / "display.json").read_text(encoding="utf-8"))
        planar_conditions = {
            "firstBarTopOffset": {"op": "ne", "parameter": "frameType", "value": "closed_frame"},
            "lastBarBottomOffset": {"op": "eq", "parameter": "frameType", "value": "two_vertical"},
        }
        conditions = {key: {'op':'all','conditions':[
            {'op':'eq','parameter':'layoutStyle','value':'planar'}, condition]}
            for key, condition in planar_conditions.items()}
        conditions['firstBarTopOffset'] = {'op':'all','conditions':[
            conditions['firstBarTopOffset'], {'op':'any','conditions':[
                {'op':'ne','parameter':'barLayoutMode','value':'center_spacing'},
                {'op':'eq','parameter':'handleEnabled','value':'yes'},
                {'op':'eq','parameter':'frameType','value':'bottom_rail'}]}]}
        declarations = {a["parameter"]: a for a in DESCRIPTOR["extensions"]["sceneSpecificationAnnotations"]["annotations"]}
        for key, condition in conditions.items():
            scene = display["views"]["scene"]["annotations"][key]
            self.assertEqual(scene["kind"], "linear")
            self.assertEqual(scene["visibleWhen"], condition)
            self.assertEqual(declarations[key]["kind"], "linear")
            self.assertEqual(declarations[key]["axis"], "z")
            self.assertEqual(declarations[key]["visibleWhen"], condition)
        # A frame may legitimately contain no inner rows; no invisible
        # first/last-row dimension may try to resolve a nonexistent endpoint.
        document = self.display(self.values(frameType="closed_frame", handleEnabled="no",
                                             barLayoutMode="fixed_count", barCount=0))
        self.assertFalse({a["parameter"] for a in document["annotations"]} & conditions.keys())
        five = self.display(self.values(layoutStyle='five_face',frameType='two_vertical'))
        self.assertFalse({a['parameter'] for a in five['annotations']} & conditions.keys())

    def test_equal_small_sections_keep_requested_corner_crossing_half_holes(self):
        # A declaration cannot decide connectivity from the flat-face width.
        # Real native final solids supply that decision after the cut.
        self.manufacturing(self.values(jointMode="male_female_half_hole",
                                      frameProfileType="rect", innerProfileType="rect"))

    def test_male_joints_only_apply_to_frame_and_horizontal_inner_members(self):
        for frame, handle in product(("two_vertical", "closed_frame", "bottom_rail"),
                                     ("left", "right", "center_double")):
            with self.subTest(frame=frame, handle=handle):
                parameters = self.values(frameType=frame, handleSide=handle, frameDepth=12,
                                         frameProfileType="rect", innerProfileType="rect",
                                         jointMode="male_female_half_hole")
                layout = self.layout(parameters)
                declaration = self.manufacturing(parameters)
                members = {m["key"]: m for m in layout["members"]}
                inserted = set()
                self.assertTrue(declaration["connections"])
                for connection in declaration["connections"]:
                    host, branch = [members[k] for k in connection["items"]]
                    eligible = (host["role"].startswith("frame.") and branch["role"].startswith("inner.")
                                and branch["start"][2] == branch["end"][2])
                    self.assertEqual(connection["properties"]["recipe"],
                                     "male_female_half_hole" if eligible else "butt_weld")
                    if eligible:
                        inserted.add(branch["key"])
                        self.assertTrue(connection["properties"]["halfHole"])
                male = {p["definition"]["targets"]["stock"] for p in declaration["processes"]
                        if p["definition"]["templateId"] == "structural-male-head"}
                self.assertEqual(male, inserted)
                self.assertTrue(male)

    def test_inactive_male_drafts_do_not_block_plain_weld_and_active_invalid_mode_rejects(self):
        baseline = self.manufacturing(self.values())
        self.assertEqual(baseline, self.manufacturing(self.values(
            maleHeadLength=-1, maleHeadWidth=-1, maleCornerType="inactive", maleCornerSize=-1,
            holeTotalClearanceDepth=-1, holeTotalClearanceHeight=-1, minimumFeatureWeb=-1)))
        with self.assertRaises(ValueError):
            self.manufacturing(self.values(jointMode="unknown"))


if __name__ == "__main__":
    unittest.main()
