from copy import deepcopy
import json
from pathlib import Path
import math
import sys
import unittest


SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template


DIRECTORY = SRC / "apps/tube-designer/templates/product/assembly_frame_lt"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {field["key"]: field["defaultValue"] for field in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "assembly-frame-lt-tests")


class AssemblyFrameLTTests(unittest.TestCase):
    def build(self, changes=None, purpose="manufacturing"):
        values = {**DEFAULTS, **(changes or {})}
        original = deepcopy(values)
        result = MODULE.generate(values, {"template": DESCRIPTOR,
                                          "geometryPurpose": purpose})
        self.assertEqual(values, original)
        self.assertEqual(result["parameters"], original)
        self.assertEqual(result["template"]["id"], DESCRIPTOR["id"])
        return result

    def assert_section_frames_replay_source(self, result):
        geometry = {node["key"]: node for node in result["geometry"]}

        def world_points(contours, origin, x_axis, y_axis):
            return sorted(tuple(round(origin[i] + x_axis[i]*point[0]
                                      + y_axis[i]*point[1], 6) for i in range(3))
                          for contour in contours for segment in contour["segments"]
                          for key in ("start", "middle", "end")
                          if (point := segment.get(key)) is not None)

        for item in result["items"]:
            properties = item["properties"]
            member = properties["assemblyFrame.member"]
            section = member["sectionFrame"]
            axis = properties["tubeDesigner.manufacturingStartToEnd"]
            x_axis, y_axis = section["xAxis"], section["yAxis"]
            cross = [x_axis[1]*y_axis[2]-x_axis[2]*y_axis[1],
                     x_axis[2]*y_axis[0]-x_axis[0]*y_axis[2],
                     x_axis[0]*y_axis[1]-x_axis[1]*y_axis[0]]
            solid = geometry[item["representations"]["result"]]
            extrusion = geometry[solid["inputs"][0]]
            profile = geometry[extrusion["inputs"][0]]
            source = solid["arguments"]["placement"]
            interval = member["stockInterval"]
            stock_origin = [section["originAtStart"][i]
                            + axis[i]*interval["startStation"] for i in range(3)]
            vector = extrusion["arguments"]["vector"]
            source_end = [source["origin"][i]
                          + source["xAxis"][i]*vector[0]
                          + source["yAxis"][i]*vector[1]
                          + source["zAxis"][i]*vector[2] for i in range(3)]
            stock_end = [stock_origin[i] + axis[i]*(interval["endStation"]
                                                    - interval["startStation"])
                         for i in range(3)]
            actual_points = sorted(world_points(profile["arguments"]["contours"],
                                                source["origin"], source["xAxis"], source["yAxis"])
                                   + world_points(profile["arguments"]["contours"],
                                                  source_end, source["xAxis"], source["yAxis"]))
            replay_points = sorted(world_points(properties["tubeDesigner.profile"]["contours"],
                                                stock_origin, x_axis, y_axis)
                                   + world_points(properties["tubeDesigner.profile"]["contours"],
                                                  stock_end, x_axis, y_axis))
            self.assertEqual(actual_points, replay_points, item["key"])
            for i in range(3):
                self.assertAlmostEqual(cross[i], axis[i], msg=item["key"])
                self.assertAlmostEqual(section["originAtStart"][i]
                                       + x_axis[i]*section["centerlineUV"][0]
                                       + y_axis[i]*section["centerlineUV"][1],
                                       member["start"][i], msg=item["key"])
        for relation in result["relationships"]:
            for anchor in relation["properties"]["participantAnchors"]:
                if anchor["kind"] != "side":
                    continue
                item = next(item for item in result["items"]
                            if item["key"] == anchor["itemKey"])
                faces = item["properties"]["assemblyFrame.member"]["sectionFrame"]["faceNormals"]
                self.assertEqual(faces[anchor["face"]], anchor["faceNormal"])

    def test_section_frames_replay_source_and_match_display(self):
        manufacturing = self.build()
        display = self.build(purpose="display")
        self.assert_section_frames_replay_source(manufacturing)
        self.assert_section_frames_replay_source(display)
        for made, shown in zip(manufacturing["items"], display["items"]):
            self.assertEqual(made["properties"]["assemblyFrame.member"]["sectionFrame"],
                             shown["properties"]["assemblyFrame.member"]["sectionFrame"])
            self.assertEqual(made["properties"]["tubeDesigner.profile"]["contours"],
                             shown["properties"]["tubeDesigner.profile"]["contours"])
        profiles = [item["properties"]["tubeDesigner.profile"]["contours"]
                    for item in manufacturing["items"][:4]]
        self.assertTrue(all(contours == profiles[0] for contours in profiles[1:]))

    def test_oblique_middle_member_replays_real_section_and_declares_angle(self):
        result = self.build({"middleAxisAngle": 60})
        self.assert_section_frames_replay_source(result)
        member = next(item for item in result["items"]
                      if item["key"] == "frame.middle.0001")
        axis = member["properties"]["tubeDesigner.manufacturingAxis"]
        self.assertAlmostEqual(axis[0], math.sin(math.radians(60)))
        self.assertAlmostEqual(axis[2], math.cos(math.radians(60)))
        relation = next(row for row in result["relationships"]
                        if row["key"] == "frame.junction.middle-left")
        host, branch = relation["properties"]["participantAnchors"]
        self.assertAlmostEqual(host["contactPoint"][2], 750 + 30 / math.sqrt(3))
        self.assertAlmostEqual(branch["anchor"]["contactInset"], 30 / axis[0])
        self.assertGreater(branch["anchor"]["stockAllowance"],
                           branch["anchor"]["contactInset"])

    def test_supplied_offset_rectangle_replays_source_without_changing_contours(self):
        def rectangle(width, height):
            points = [[-width / 2, 3 - height / 2], [width / 2, 3 - height / 2],
                      [width / 2, 3 + height / 2], [-width / 2, 3 + height / 2]]
            return {"kind": "path", "closed": True, "segments": [
                {"kind": "line", "start": points[i], "end": points[(i + 1) % 4]}
                for i in range(4)]}

        contours = [rectangle(20, 20), rectangle(16, 16)]
        profile = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                   "kind": "fixed-section", "profileForm": "fixed",
                   "sectionKind": "rect", "name": "偏置矩形管", "specification": "20×20×2",
                   "contentDigest": "offset-rect-test", "width": 20, "depth": 20,
                   "wallThickness": 2, "geometrySource": "providedBoundary",
                   "contours": contours}
        changes = {"tubeDesignerProfileOverrides": {"frame": profile}}
        for purpose in ("manufacturing", "display"):
            result = self.build(changes, purpose=purpose)
            self.assert_section_frames_replay_source(result)
            for item in result["items"][:4]:
                self.assertEqual(item["properties"]["tubeDesigner.profile"]["contours"],
                                 contours)

    def test_reversed_logical_datum_replays_unchanged_physical_blank(self):
        def reverse_face(face):
            return {"top": "bottom", "bottom": "top"}.get(face, face)

        for purpose in ("manufacturing", "display"):
            result = self.build(purpose=purpose)
            item = next(item for item in result["items"]
                        if item["key"] == "frame.right.0001")
            properties = item["properties"]
            member = properties["assemblyFrame.member"]
            start, end = member["start"], member["end"]
            length = member["axisLength"]
            section = member["sectionFrame"]
            section["originAtStart"] = [section["originAtStart"][i] + end[i] - start[i]
                                        for i in range(3)]
            section["yAxis"] = [-value for value in section["yAxis"]]
            section["centerlineUV"][1] *= -1
            faces = section["faceNormals"]
            faces["top"], faces["bottom"] = faces["bottom"], faces["top"]
            interval = member["stockInterval"]
            old_start, old_end = interval["startStation"], interval["endStation"]
            interval["startStation"], interval["endStation"] = length - old_end, length - old_start
            member["start"], member["end"] = end, start
            properties["tubeDesigner.manufacturingStartToEnd"] = [
                -value for value in properties["tubeDesigner.manufacturingStartToEnd"]]
            for relation in result["relationships"]:
                for anchor in relation["properties"]["participantAnchors"]:
                    if anchor["itemKey"] != item["key"]:
                        continue
                    declaration = anchor["anchor"]
                    if anchor["kind"] == "end":
                        old_end = declaration["end"]
                        new_end = "end" if old_end == "start" else "start"
                        declaration["end"] = anchor["end"] = new_end
                        anchor["localAxialStation"] = length if old_end == "start" else 0.0
                        if "approachFace" in declaration:
                            declaration["approachFace"] = reverse_face(declaration["approachFace"])
                    else:
                        declaration["station"] = length - declaration["station"]
                        anchor["localAxialStation"] = declaration["station"]
                        declaration["face"] = anchor["face"] = reverse_face(anchor["face"])
            self.assert_section_frames_replay_source(result)

    def test_five_independent_uncut_tubes_and_six_connections(self):
        result = self.build()
        self.assertEqual([item["key"] for item in result["items"]], [
            "frame.left.0001", "frame.right.0001", "frame.bottom.0001",
            "frame.top.0001", "frame.middle.0001"])
        self.assertEqual(len(result["tables"][0]["rows"]), 5)
        self.assertEqual(result["outputs"][0]["items"],
                         [item["key"] for item in result["items"]])
        self.assertFalse(any(node["operator"] == "boolean" for node in result["geometry"]))
        self.assertTrue(all(item["properties"]["assemblyFrame.member"]["stockState"] == "uncut"
                            and item["properties"]["tubeDesigner.endProcess"]["startCut"] == "square"
                            and item["properties"]["tubeDesigner.endProcess"]["endCut"] == "square"
                            for item in result["items"]))
        geometry = {node["key"]: node for node in result["geometry"]}
        for item in result["items"]:
            properties = item["properties"]
            frame = properties["assemblyFrame.member"]
            interval = frame["stockInterval"]
            self.assertAlmostEqual(properties["length"],
                                   interval["endStation"]-interval["startStation"])
            self.assertAlmostEqual(math.dist(frame["start"],frame["end"]),frame["axisLength"])
            solid = geometry[item["representations"]["result"]]
            extrusion = geometry[solid["inputs"][0]]
            self.assertAlmostEqual(math.dist([0,0,0],extrusion["arguments"]["vector"]),
                                   properties["length"])
        self.assertEqual(result["items"][0]["properties"]["tubeDesigner.manufacturingAxis"],
                         [0.0, 0.0, 1.0])
        self.assertEqual(result["items"][2]["properties"]["tubeDesigner.manufacturingAxis"],
                         [1.0, 0.0, 0.0])
        self.assertTrue(all(item["properties"]["tubeDesigner.manufacturingStartToEnd"]
                            == item["properties"]["tubeDesigner.manufacturingAxis"]
                            for item in result["items"]))
        self.assertEqual([rel["properties"]["topology"] for rel in result["relationships"]],
                         ["L", "L", "L", "L", "T", "T"])
        self.assertEqual({rel["key"] for rel in result["relationships"]}, {
            "frame.corner.bottom-left", "frame.corner.bottom-right",
            "frame.corner.top-left", "frame.corner.top-right",
            "frame.junction.middle-left", "frame.junction.middle-right"})

    def test_same_real_member_has_distinct_l_and_t_anchors(self):
        result = self.build()
        connections = {rel["key"]: rel for rel in result["relationships"]}
        corner = connections["frame.corner.bottom-left"]
        tee = connections["frame.junction.middle-left"]
        self.assertEqual(corner["items"], ["frame.left.0001", "frame.bottom.0001"])
        self.assertEqual(tee["items"], ["frame.left.0001", "frame.middle.0001"])
        corner_end = corner["properties"]["participantAnchors"][0]
        self.assertEqual(corner_end["itemKey"], "frame.left.0001")
        self.assertEqual(corner_end["anchor"], {"kind": "end", "end": "start",
                                                "approachFace": "bottom", "stockAllowance": 30.0,
                                                "contactInset": 30.0})
        host = tee["properties"]["participantAnchors"][0]
        self.assertEqual(host["itemKey"], "frame.left.0001")
        self.assertEqual(host["anchor"], {"kind": "side", "face": "bottom",
                                         "reference": "start", "station": 720})
        self.assertEqual(host["localAxialStation"], 720)
        self.assertEqual(host["centerlinePoint"], [30, 0, 750])
        self.assertEqual(host["faceNormal"], [1, 0, 0])
        self.assertEqual(host["contactPoint"], [60, 0, 750])
        self.assertEqual(tee["properties"]["participantAnchors"][1]["anchor"],
                         {"kind": "end", "end": "start", "stockAllowance": 30.0,
                          "contactInset": 30.0})
        right_tee = connections["frame.junction.middle-right"]
        right_host = right_tee["properties"]["participantAnchors"][0]
        self.assertEqual(right_host["anchor"], {"kind": "side", "face": "top",
                                               "reference": "start", "station": 720})
        self.assertEqual(right_host["faceNormal"], [-1, 0, 0])

    def test_dimension_edits_keep_stable_connections_and_independent_sections(self):
        before = self.build(purpose="display")
        changed = self.build({"width": 1400, "height": 1800, "middleHeight": 620,
                              "middleWidth": 24}, purpose="display")
        self.assertEqual([rel["key"] for rel in changed["relationships"]],
                         [rel["key"] for rel in before["relationships"]])
        profiles = {item["key"]: item["properties"]["tubeDesigner.profile"]
                    for item in changed["items"]}
        self.assertEqual(profiles["frame.left.0001"]["width"], 60)
        self.assertEqual(profiles["frame.middle.0001"]["width"], 24)
        tee = next(rel for rel in changed["relationships"]
                   if rel["key"] == "frame.junction.middle-left")
        self.assertEqual(tee["properties"]["participantAnchors"][0]["localAxialStation"], 590)
        self.assertFalse(any(node["operator"] == "boolean" for node in changed["geometry"]))

    def test_invalid_middle_height_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "中横档"):
            self.build({"middleHeight": 65})


if __name__ == "__main__":
    unittest.main()
