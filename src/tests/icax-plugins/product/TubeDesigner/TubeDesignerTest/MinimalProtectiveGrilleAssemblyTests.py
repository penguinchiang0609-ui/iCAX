"""A shipped window product can leave every L/T node to assembly templates."""
import json
import math
from pathlib import Path
import sys
import unittest


SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template


DIRECTORY = SRC / "apps/tube-designer/templates/product/minimal_protective_grille"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {field["key"]: field["defaultValue"] for field in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "minimal-grille-assembly-tests")


class MinimalProtectiveGrilleAssemblyTests(unittest.TestCase):
    def build(self, overrides=None, purpose="manufacturing"):
        values = {**DEFAULTS, "frameType": "closed_frame",
                  "assemblyPlanningMode": "external_templates",
                  "barLayoutMode": "fixed_count", "barCount": 1,
                  "handleEnabled": False, **(overrides or {})}
        result = MODULE.generate(values, {"template": DESCRIPTOR,
                                          "geometryPurpose": purpose})
        self.assertEqual(result["parameters"], values)
        self.assertEqual(result["template"]["version"], DESCRIPTOR["version"])
        return result

    def test_real_existing_product_exposes_uncut_l_and_t_nodes(self):
        result = self.build()
        self.assertEqual(len(result["items"]), 5)
        self.assertFalse(any(node["operator"] == "boolean" for node in result["geometry"]))
        self.assertTrue(all(item["representations"]["result"].endswith(".solid")
                            for item in result["items"]))
        self.assertTrue(all(item["properties"]["tubeDesigner.assemblyPlanning"]
                            == {"stockState": "uncut", "ready": False}
                            for item in result["items"]))
        self.assertEqual([relationship["properties"]["topology"]
                          for relationship in result["relationships"]],
                         ["L"] * 4 + ["T"] * 2)
        by_key = {relationship["key"]: relationship for relationship in result["relationships"]}
        corner = by_key["frame.corner.bottom-left"]
        self.assertEqual(corner["items"], ["frame.left.0001", "frame.bottom.0001"])
        self.assertEqual([a["anchor"] for a in corner["properties"]["participantAnchors"]], [
            {"kind": "end", "end": "start", "approachFace": "bottom", "stockAllowance": 10.0, "contactInset": 10.0},
            {"kind": "end", "end": "start", "approachFace": "top", "stockAllowance": 10.0, "contactInset": 10.0},
        ])
        tee = by_key["frame.junction.bar-0001-left"]
        host, branch = tee["properties"]["participantAnchors"]
        self.assertEqual(host["anchor"], {"kind": "side", "face": "bottom",
                                          "reference": "start", "station": host["localAxialStation"]})
        self.assertEqual(host["centerlinePoint"], branch["centerlinePoint"])
        self.assertEqual(branch["anchor"], {"kind": "end", "end": "start", "stockAllowance": 10.0})
        right = by_key["frame.junction.bar-0001-right"]
        self.assertEqual(right["properties"]["participantAnchors"][0]["anchor"]["face"], "top")
        self.assertEqual(right["properties"]["participantAnchors"][1]["anchor"]["end"], "end")
        items = {item["key"]: item for item in result["items"]}
        geometry = {node["key"]: node for node in result["geometry"]}
        for item in result["items"]:
            props = item["properties"]
            frame = props["assemblyFrame.member"]
            interval = frame["stockInterval"]
            self.assertAlmostEqual(props["length"],interval["endStation"]-interval["startStation"])
            solid = geometry[item["representations"]["result"]]
            extrusion = geometry[solid["inputs"][0]]
            self.assertAlmostEqual(math.dist([0,0,0],extrusion["arguments"]["vector"]),props["length"])
            section = frame["sectionFrame"]
            x_axis, y_axis = section["xAxis"], section["yAxis"]
            axis = props["tubeDesigner.manufacturingStartToEnd"]
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
        frames = {item["key"]: item["properties"]["tubeDesigner.profile"]["contours"]
                  for item in result["items"] if item["key"].startswith("frame.")}
        self.assertEqual(frames["frame.left.0001"], frames["frame.bottom.0001"])
        self.assertEqual(frames["frame.left.0001"], frames["frame.top.0001"])
        for connection in result["relationships"]:
            node_point = connection["properties"]["centerlinePoint"]
            for participant in connection["properties"]["participantAnchors"]:
                item = items[participant["itemKey"]]
                transform = geometry[item["representations"]["result"]]
                placement = transform["arguments"]["placement"]
                source_direction = item["properties"]["tubeDesigner.manufacturingStartToEnd"]
                anchor = participant["anchor"]
                interval = item["properties"]["assemblyFrame.member"]["stockInterval"]
                distance = (anchor["station"] if anchor["kind"] == "side" else
                            0.0 if anchor["end"] == "start" else
                            item["properties"]["assemblyFrame.member"]["axisLength"])
                actual = [placement["origin"][i] + source_direction[i]*(distance-interval["startStation"])
                          for i in range(3)]
                for coordinate in range(3):
                    self.assertAlmostEqual(actual[coordinate], node_point[coordinate], places=6,
                                           msg=connection["key"])
                if anchor["kind"] == "end":
                    self.assertAlmostEqual(anchor["stockAllowance"],
                        -interval["startStation"] if anchor["end"] == "start" else
                        interval["endStation"]-item["properties"]["assemblyFrame.member"]["axisLength"])
                else:
                    section = item["properties"]["assemblyFrame.member"]["sectionFrame"]
                    self.assertEqual(section["faceNormals"][anchor["face"]],
                                     participant["faceNormal"])

    def test_reversed_upper_member_uses_source_end_and_manufacturing_face(self):
        result = self.build()
        top = next(item for item in result["items"] if item["key"] == "frame.top.0001")
        self.assertEqual(top["properties"]["tubeDesigner.manufacturingStartToEnd"],
                         [-1.0, 0.0, 0.0])
        by_key = {relationship["key"]: relationship for relationship in result["relationships"]}
        left_top = by_key["frame.corner.top-left"]["properties"]["participantAnchors"][1]
        right_top = by_key["frame.corner.top-right"]["properties"]["participantAnchors"][1]
        self.assertEqual(left_top["anchor"],
                         {"kind": "end", "end": "end", "approachFace": "bottom", "stockAllowance": 10.0, "contactInset": 10.0})
        self.assertEqual(right_top["anchor"],
                         {"kind": "end", "end": "start", "approachFace": "bottom", "stockAllowance": 10.0, "contactInset": 10.0})
        self.assertEqual(left_top["centerlinePoint"], [10.0, 0, 1490.0])
        self.assertEqual(right_top["centerlinePoint"], [1190.0, 0, 1490.0])

    def test_supplied_offset_rectangle_keeps_canonical_contours_and_replays_datum(self):
        def rectangle(width, height):
            points = [[-width/2, 3-height/2], [width/2, 3-height/2],
                      [width/2, 3+height/2], [-width/2, 3+height/2]]
            return {"kind": "path", "closed": True, "segments": [
                {"kind": "line", "start": points[i], "end": points[(i+1) % 4]}
                for i in range(4)]}
        contours = [rectangle(20, 20), rectangle(16, 16)]
        profile = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                   "kind": "fixed-section", "profileForm": "fixed",
                   "sectionKind": "rect", "name": "偏置矩形管", "specification": "20×20×2",
                   "contentDigest": "offset-rect-test", "width": 20, "depth": 20,
                   "wallThickness": 2, "geometrySource": "providedBoundary",
                   "contours": contours}
        result = self.build({"tubeDesignerProfileOverrides": {"frame": profile}})
        items = {item["key"]: item for item in result["items"]}
        bottom = items["frame.bottom.0001"]["properties"]
        top = items["frame.top.0001"]["properties"]
        self.assertEqual(bottom["tubeDesigner.profile"]["contours"], contours)
        self.assertEqual(top["tubeDesigner.profile"]["contours"], contours)
        self.assertEqual(bottom["assemblyFrame.member"]["sectionFrame"]["centerlineUV"], [0.0, 6.0])
        self.assertEqual(top["assemblyFrame.member"]["sectionFrame"]["centerlineUV"], [0.0, 0.0])

    def test_inactive_old_process_drafts_do_not_cut_or_reject(self):
        result = self.build({"maleHeadLength": 120, "installHoleEnabled": True,
                             "installHoleLargeDiameter": 200,
                             "frameCornerJoint": "miter_45"})
        self.assertFalse(any(node["operator"] == "boolean" for node in result["geometry"]))
        self.assertEqual(result["extensions"]["protectiveGrille"]["jointRecipe"], "unassigned")
        self.assertEqual(result["extensions"]["protectiveGrille"]["cornerRecipe"], "unassigned")
        self.assertEqual(result["extensions"]["protectiveGrille"]["installation"]["enabled"], False)
        self.assertEqual(result["parameters"]["maleHeadLength"], 120)
        self.assertEqual(result["parameters"]["installHoleEnabled"], True)

    def test_product_dimensions_relocate_nodes_without_changing_process_recipe(self):
        result = self.build({"width": 1600, "height": 1800, "frameWidth": 30,
                             "barCount": 2, "installHoleEnabled": False})
        self.assertEqual(len(result["relationships"]), 8)
        by_key = {relationship["key"]: relationship for relationship in result["relationships"]}
        self.assertEqual(by_key["frame.corner.top-left"]["properties"]["centerlinePoint"],
                         [15.0, 0, 1785.0])
        self.assertEqual(by_key["frame.corner.top-right"]["properties"]["centerlinePoint"],
                         [1585.0, 0, 1785.0])
        self.assertEqual(by_key["frame.corner.top-left"]["properties"]["participantAnchors"][1]
                         ["localAxialStation"], 1570.0)
        self.assertFalse(any("recipe" in relationship["properties"]
                             for relationship in result["relationships"]))


if __name__ == "__main__":
    unittest.main()
