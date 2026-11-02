import json
from pathlib import Path
import sys
import unittest


SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template


def package():
    directory = SRC / "apps/tube-designer/templates/product/minimal_protective_grille"
    descriptor = json.loads((directory / "template.json").read_text(encoding="utf-8"))
    defaults = {field["key"]: field["defaultValue"] for field in descriptor["parameters"]}
    module = _load_template(str(directory / "template.py"), "minimal-protective-grille-tests")
    return descriptor, defaults, module


def process_result(document, instance_id):
    records=document["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]
    return next(record["result"] for record in records if record["instanceId"]==instance_id)


def process_geometry(document, instance_id):
    return {node["key"]:node for node in process_result(document,instance_id)["geometry"]}


class MinimalProtectiveGrilleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.module = package()

    def build(self, changes=None, purpose="manufacturing"):
        parameters = {**self.defaults, **(changes or {})}
        result = self.module.generate(
            parameters, {"template": self.descriptor, "geometryPurpose": purpose})
        self.assertEqual(result["parameters"], parameters)
        return result

    def test_default_has_purpose_specific_geometry_and_grouped_bom(self):
        display = self.build(purpose="display")
        manufacturing = self.build(purpose="manufacturing")
        self.assertEqual(len(display["items"]), 12)
        self.assertEqual(len(manufacturing["items"]), 12)
        self.assertFalse(any(node["operator"] == "boolean" for node in display["geometry"]))
        self.assertTrue(any(node["operator"] == "boolean" for node in manufacturing["geometry"]))
        extension = manufacturing["extensions"]["protectiveGrille"]
        self.assertLessEqual(extension["actualMaximumClearGap"], self.defaults["maximumClearGap"])
        self.assertEqual(len(extension["barCenters"]), 10)
        self.assertEqual(extension["handle"]["side"], "right")
        rows = manufacturing["tables"][0]["rows"]
        self.assertEqual(sorted(row["values"]["quantity"] for row in rows), [2, 10])
        self.assertEqual(manufacturing["outputs"], [{
            "key": "result", "purpose": "result",
            "items": [item["key"] for item in manufacturing["items"]], "properties": {},
        }])

    def test_model_identity_matches_the_shipped_descriptor(self):
        descriptor = {**self.descriptor, "packageDigest": "identity-regression"}
        for purpose in ("display", "manufacturing"):
            with self.subTest(purpose=purpose):
                model = self.module.generate(dict(self.defaults), {
                    "template": descriptor, "geometryPurpose": purpose})
                self.assertEqual(model["template"], {
                    "id": descriptor["id"], "version": descriptor["version"],
                    "packageDigest": descriptor["packageDigest"],
                })

    def test_all_layout_modes_and_handle_reference_modes(self):
        fixed = self.build({"barLayoutMode": "fixed_count", "barCount": 7,
                            "handleEnabled": False, "installHoleEnabled": False}, "display")
        self.assertEqual(len(fixed["extensions"]["protectiveGrille"]["barCenters"]), 7)
        pitch = self.build({"barLayoutMode": "fixed_pitch", "barPitch": 125,
                            "pitchAlignment": "bottom", "handleEnabled": False,
                            "installHoleEnabled": False}, "display")
        centers = pitch["extensions"]["protectiveGrille"]["barCenters"]
        self.assertTrue(all(abs((b-a)-125) < 1.0e-7 for a, b in zip(centers, centers[1:])))
        for reference, reference_y, expected in (
                ("bottom", 700, [675, 825]),
                ("center", 750, [675, 825]),
                ("top", 800, [675, 825])):
            with self.subTest(reference=reference):
                result = self.build({"handleReference": reference,
                                     "handleReferenceY": reference_y,
                                     "installHoleEnabled": False}, "display")
                self.assertEqual(result["extensions"]["protectiveGrille"]["handleKeepout"], expected)

    def test_uniform_remove_reports_removed_rods(self):
        result = self.build({"barLayoutMode": "fixed_pitch", "barPitch": 120,
                             "handleDistribution": "uniform_remove",
                             "handleReference": "center", "handleReferenceY": 720,
                             "handleHeight": 100, "installHoleEnabled": False}, "display")
        extension = result["extensions"]["protectiveGrille"]
        self.assertTrue(extension["removedBarCenters"])
        low, high = extension["handleKeepout"]
        self.assertTrue(all(center + self.defaults["innerWidth"]/2 <= low
                            or center - self.defaults["innerWidth"]/2 >= high
                            for center in extension["barCenters"]))

    def test_two_vertical_and_every_closed_frame_corner_recipe(self):
        two = self.build({"installHoleEnabled": False}, "display")
        self.assertEqual(two["extensions"]["protectiveGrille"]["frameType"], "two_vertical")
        for recipe in ("miter_45", "horizontal_wrap", "vertical_wrap"):
            with self.subTest(recipe=recipe):
                result = self.build({"frameType": "closed_frame", "frameCornerJoint": recipe,
                                     "installHoleEnabled": False}, "manufacturing")
                self.assertEqual(result["extensions"]["protectiveGrille"]["cornerRecipe"], recipe)
                self.assertEqual(len([item for item in result["items"]
                                      if item["key"].startswith("frame.")]), 4)
                self.assertEqual(len([relationship for relationship in result["relationships"]
                                      if relationship["key"].startswith("frame.corner.")]), 4)

    def test_half_holes_cut_only_near_wall_and_male_tips_are_mirrored(self):
        result = self.build({"installHoleEnabled": False, "maleCornerType": "chamfer"})
        geometry = process_geometry(result,"frame.left.0001.apertures")
        left_cutters = [node for node in geometry.values() if node["operator"]=="extrude"]
        self.assertEqual(len(left_cutters), 10)
        self.assertTrue(all(abs(node["arguments"]["vector"][0]
                                - (self.defaults["frameWallThickness"] + 2e-7)) < 1e-9
                            for node in left_cutters))
        heads=process_geometry(result,"inner.bar.0001.male-head")
        left_points = [segment["start"] for segment in heads["left.profile"]["arguments"]["contours"][0]["segments"]]
        right_points = [segment["start"] for segment in heads["right.profile"]["arguments"]["contours"][0]["segments"]]
        self.assertGreater(left_points[1][0], left_points[0][0])
        self.assertLess(right_points[1][0], right_points[2][0])
        self.assertTrue(all(relationship["properties"]["halfHole"]
                            for relationship in result["relationships"]
                            if relationship["key"].startswith("joint.")))

    def test_every_male_corner_choice_changes_processing_without_changing_finished_stock(self):
        display=None
        for corner in ("round","chamfer","square"):
            with self.subTest(corner=corner):
                current=self.build({"maleCornerType":corner,"installHoleEnabled":False},"display")
                if display is None:display=current["geometry"]
                else:self.assertEqual(current["geometry"],display)
                manufactured=self.build({"maleCornerType":corner,"installHoleEnabled":False})
                heads=process_geometry(manufactured,"inner.bar.0001.male-head")
                segments=heads["left.profile"]["arguments"]["contours"][0]["segments"]
                self.assertEqual(any(segment["kind"]=="arc" for segment in segments),corner=="round")
                self.assertEqual(process_result(manufactured,"inner.bar.0001.male-head")["operations"][0]["operation"],"intersect")

    def test_installation_holes_have_two_faces_and_explicit_conflict_policy(self):
        result = self.build({"handleEnabled": False})
        installation=[]
        for side in ("left","right"):
            geometry=process_geometry(result,f"frame.{side}.0001.apertures")
            installation.extend(node for node in geometry.values() if node["operator"]=="profile2d"
                                and all(segment["kind"]=="arc" for segment in node["arguments"]["contours"][0]["segments"]))
        self.assertEqual(len(installation),12)
        self.assertEqual(len(result["tables"][0]["rows"]), 2,
                         "front holes leave the left and right rails as identical parts")
        with self.assertRaisesRegex(ValueError, "冲突"):
            self.build({"installHoleCountPerSide": 1, "installHoleBottomOffset": 112.5,
                        "installHoleOrientation": "side", "installHoleAutoAvoid": False})
        avoided = self.build({"installHoleCountPerSide": 1, "installHoleBottomOffset": 112.5,
                              "installHoleOrientation": "side", "installHoleAutoAvoid": True,
                              "installHoleMaximumShift": 30})
        installation = avoided["extensions"]["protectiveGrille"]["installation"]
        self.assertNotEqual(installation["positionsBySide"]["left"], [112.5])
        self.assertTrue(installation["adjustmentsBySide"]["left"])

    def test_side_installation_keeps_mirrored_verticals_as_separate_bom_parts(self):
        result = self.build({"handleEnabled": False,
                             "installHoleOrientation": "side",
                             "installHoleAutoAvoid": True,
                             "installHoleMaximumShift": 30})
        rows = result["tables"][0]["rows"]
        self.assertEqual(sorted(row["values"]["quantity"] for row in rows), [1, 1, 12])

    def test_half_hole_rejects_non_rectangular_profile_by_actual_section_capability(self):
        round_override = {
            "schema": "icax.imported-tube-profile", "schemaVersion": 1,
            "kind": "fixed-section", "profileForm": "fixed", "sectionKind": "round",
            "name": "圆管", "specification": "Φ15×1", "contentDigest": "test-round",
            "width": 15, "depth": 15, "wallThickness": 1,
            "contours": [{"kind": "circle", "radius": 7.5},
                         {"kind": "circle", "radius": 6.5}],
        }
        with self.assertRaisesRegex(ValueError, "方矩管面"):
            self.build({"tubeDesignerProfileOverrides": {"inner": round_override}})

    def test_profile_orientation_rotates_real_section_and_joint_dimensions(self):
        result = self.build({"innerProfileOrientation": "rotate_90",
                             "handleEnabled": False, "installHoleEnabled": False})
        bar = next(item for item in result["items"] if item["key"] == "inner.bar.0001")
        profile = bar["properties"]["tubeDesigner.profile"]
        self.assertEqual(profile["sectionOrientation"], "rotate_90")
        self.assertEqual((profile["width"], profile["depth"]),
                         (self.defaults["innerDepth"], self.defaults["innerWidth"]))
        joint = next(relationship for relationship in result["relationships"]
                     if relationship["key"] == "joint.0001.left")
        self.assertAlmostEqual(joint["properties"]["totalClearanceDepth"],
                               self.defaults["holeTotalClearanceDepth"])
        cutter = process_geometry(result,"frame.left.0001.apertures")["hole.0.profile"]
        contour = cutter["arguments"]["contours"][0]
        points = [segment["start"] for segment in contour["segments"]]
        self.assertAlmostEqual(max(point[0] for point in points) - min(point[0] for point in points),
                               self.defaults["innerWidth"] + self.defaults["holeTotalClearanceDepth"])
        self.assertAlmostEqual(max(point[1] for point in points) - min(point[1] for point in points),
                               self.defaults["innerDepth"] + self.defaults["holeTotalClearanceHeight"])

    def test_descriptor_uses_three_semantic_sections_and_library_profiles(self):
        sections = self.descriptor["extensions"]["parameterLayout"]["sections"]
        self.assertEqual([section["key"] for section in sections],
                         ["product", "materials", "process"])
        selectors = [field for field in self.descriptor["parameters"]
                     if field.get("presentation", {}).get("editor") == "profile-library"]
        self.assertEqual({field["presentation"]["resourceRole"] for field in selectors},
                         {"frame", "inner"})
        self.assertTrue(all(field["presentation"]["profileConstraints"]["hollow"]
                            for field in selectors))


if __name__ == "__main__":
    unittest.main()
