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
        geometry = {node["key"]: node for node in result["geometry"]}
        left_cutters = [node for key, node in geometry.items()
                        if key.startswith("frame.left.0001.half-hole.") and key.endswith(".solid")]
        self.assertEqual(len(left_cutters), 10)
        self.assertTrue(all(abs(node["arguments"]["vector"][0]
                                - (self.defaults["frameWallThickness"] + 2e-7)) < 1e-9
                            for node in left_cutters))
        left_points = geometry["inner.bar.0001.male.left.profile"]["arguments"]["contours"][0]["points"]
        right_points = geometry["inner.bar.0001.male.right.profile"]["arguments"]["contours"][0]["points"]
        self.assertGreater(left_points[1][0], left_points[0][0])
        self.assertLess(right_points[1][0], right_points[2][0])
        self.assertTrue(all(relationship["properties"]["halfHole"]
                            for relationship in result["relationships"]
                            if relationship["key"].startswith("joint.")))

    def test_flat_weld_removes_all_half_hole_and_male_head_geometry(self):
        result = self.build({"innerJoint": "flat_weld", "installHoleEnabled": False})
        keys = [node["key"] for node in result["geometry"]]
        self.assertFalse(any("half-hole" in key or ".male." in key for key in keys))
        self.assertFalse(any(node["operator"] == "boolean" for node in result["geometry"]))
        self.assertTrue(all(not relationship["properties"]["halfHole"]
                            for relationship in result["relationships"]
                            if relationship["key"].startswith("joint.")))

    def test_installation_holes_have_two_faces_and_explicit_conflict_policy(self):
        result = self.build({"innerJoint": "flat_weld", "handleEnabled": False})
        keys = [node["key"] for node in result["geometry"]]
        self.assertEqual(len([key for key in keys if ".install." in key and key.endswith(".solid")]), 12)
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
        result = self.build({"innerJoint": "flat_weld", "handleEnabled": False,
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
        cutter = next(node for node in result["geometry"]
                      if node["key"] == "frame.left.0001.half-hole.0001.profile")
        contour = cutter["arguments"]["contours"][0]
        self.assertAlmostEqual(contour["width"],
                               self.defaults["innerWidth"] + self.defaults["holeTotalClearanceDepth"])
        self.assertAlmostEqual(contour["height"],
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
