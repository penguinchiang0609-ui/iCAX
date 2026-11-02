"""Installation screws enter the large inner hole and bear on the small outer wall.

The product declares paired side-wall cuts; the shared executor performs them.
These tests do not substitute for native STEP topology or screw-load validation.
"""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleInstallationHoleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package("minimal_protective_grille")

    def values(self, **changes):
        # Keep weld/receiver footprints clear of the intended 50, 625, 1200 holes.
        # Default rows at 55 and 1195 would overlap these mounting-hole mouths.
        return {**self.defaults, "frameWidth": 40, "frameDepth": 30,
                "frameCornerRadius": 2, "frameWallThickness": 1.5,
                "handleEnabled": "no", "barLayoutMode": "fixed_count", "barCount": 2,
                "firstBarTopOffset": 100, "lastBarBottomOffset": 100,
                "installHoleEnabled": "yes", "installHoleOrientation": "side",
                "installHoleCountPerSide": 3,
                "installHoleTopOffset": 50, "installHoleBottomOffset": 50,
                "installHoleLargeDiameter": 6, "installHoleSmallDiameter": 4,
                "installHoleMinimumWeb": 0.2, **changes}

    def declare(self, values):
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor), forbid_processing():
            declaration = self.template.manufacturing(values)
        self.assertEqual(values, before)
        validate_manufacturing_model(declaration)
        return declaration

    def execute(self, values):
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            layout = self.template.layout(values)
            design = self.template.display(values)
            declaration = self.declare(values)
            frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
            composed = compose_manufacturing_model(declaration, design)
            self.assertEqual({m["itemKey"] for m in composed["sourceMappings"]},
                             {m["key"] for m in layout["members"]})
            actual = execute_definition(declaration, design=design)
        self.assertEqual(values, before)
        self.assertEqual(design, frozen_design)
        self.assertEqual(declaration, frozen_declaration)
        preparations = {p["itemKey"]: p for p in
                        actual["extensions"]["tubeDesigner.workpiecePreparations"]}
        self.assertEqual(set(preparations), {m["key"] for m in layout["members"]})
        self.assertTrue(all(p["ready"] and p["status"] == "prepared" for p in preparations.values()))
        return layout, declaration, actual, preparations

    @staticmethod
    def apertures(declaration):
        return [p for p in declaration["processes"]
                if p["definition"]["templateId"] == "structural-apertures"]

    def assert_rejected(self, **changes):
        values = self.values(**changes)
        before = deepcopy(values)
        with self.assertRaises(ValueError):
            self.declare(values)
        self.assertEqual(values, before, "A rejected layout must not move holes or rewrite inputs")

    def assert_pairs(self, values, layout, declaration, centers):
        facts = layout["facts"]["installationHoles"]
        self.assertTrue(facts["enabled"])
        self.assertEqual(facts["countPerSide"], len(centers))
        self.assertEqual(len(facts["centers"]), len(centers))
        for actual, expected in zip(facts["centers"], centers):
            self.assertAlmostEqual(actual, expected, places=7)
        members = {m["key"]: m for m in layout["members"]}
        self.assertEqual(len(facts["groups"]), 2 * len(centers))
        for side in ("left", "right"):
            groups = sorted((g for g in facts["groups"] if g["side"] == side),
                            key=lambda g: g["point"][2])
            self.assertEqual(len(groups), len(centers))
            for group, z in zip(groups, centers):
                member = members[group["memberKey"]]
                self.assertEqual(member["role"], "frame." + side)
                self.assertAlmostEqual(group["point"][2], z, places=7)
                self.assertAlmostEqual(group["station"], z - member["start"][2], places=7)

        circular = []
        for process in self.apertures(declaration):
            call = process["definition"]
            member = members[call["targets"]["stock"]]
            circles = [h for h in call["processInput"]["geometry"]["holes"] if "diameter" in h]
            if circles:
                self.assertIn(member["role"], ("frame.left", "frame.right"))
                self.assertEqual(member["start"][0], member["end"][0])
                circular.extend((member, hole) for hole in circles)
        self.assertEqual(len(circular), 4 * len(centers))
        for side in ("left", "right"):
            side_holes = [(m, h) for m, h in circular if m["role"] == "frame." + side]
            self.assertEqual(len(side_holes), 2 * len(centers))
            for z in centers:
                pair = [(m, h) for m, h in side_holes if abs(h["placement"]["origin"][2] - z) < 1.e-6]
                self.assertEqual(len(pair), 2)
                by_diameter = {h["diameter"]: (m, h) for m, h in pair}
                self.assertEqual(set(by_diameter), {values["installHoleLargeDiameter"], values["installHoleSmallDiameter"]})
                front = values["installHoleOrientation"] == "front"
                if front:
                    # The product front is world -Y. Both uprights receive the
                    # large entry hole there and the small bearing hole at +Y.
                    self.assertAlmostEqual(pair[0][1]["placement"]["origin"][1],
                                           -pair[1][1]["placement"]["origin"][1], places=7)
                for large in (True, False):
                    diameter = values["installHoleLargeDiameter" if large else "installHoleSmallDiameter"]
                    member, hole = by_diameter[diameter]
                    origin, vector = hole["placement"]["origin"], hole["vector"]
                    width = member["profile"].width
                    if front:
                        normal_index = 1
                        face = member["profile"].depth / 2
                        surface = -face if large else face
                        direction = 1 if large else -1
                        normal_span = member["profile"].depth
                        self.assertAlmostEqual(origin[0], member["start"][0], places=7)
                    else:
                        normal_index, normal_span = 0, width
                        if side == "left":
                            surface, direction = (width, -1) if large else (0, 1)
                        else:
                            surface, direction = ((values["width"] - width, 1) if large
                                                  else (values["width"], -1))
                        self.assertAlmostEqual(origin[1], 0, places=7)
                    self.assertAlmostEqual(origin[normal_index], surface, delta=1.e-6)
                    self.assertAlmostEqual(origin[2], z, places=7)
                    self.assertGreater(direction * vector[normal_index], 0)
                    for index in range(3):
                        if index != normal_index:
                            self.assertAlmostEqual(vector[index], 0, places=7)
                    self.assertAlmostEqual(math.hypot(*vector), member["profile"].wall + 2.e-7, places=8)
                    self.assertLess(math.hypot(*vector), normal_span / 2,
                                    "Each tool must stop after its own wall, not enlarge the opposite hole")
                    x, y = (hole["placement"][k] for k in ("xAxis", "yAxis"))
                    self.assertAlmostEqual(x[normal_index], 0, places=7)
                    self.assertAlmostEqual(y[normal_index], 0, places=7)
                    self.assertAlmostEqual(math.hypot(*x), 1, places=7)
                    self.assertAlmostEqual(math.hypot(*y), 1, places=7)
                    self.assertAlmostEqual(sum(a*b for a, b in zip(x, y)), 0, places=7)

    def test_defaults_keep_installation_disabled_and_ordinary_manufacturing_unchanged(self):
        self.assertEqual(self.defaults["installHoleEnabled"], "no")
        self.assertEqual(self.defaults["installHoleOrientation"], "side")
        expected = {"installHoleCountPerSide": 3, "installHoleTopOffset": 50,
                    "installHoleBottomOffset": 50, "installHoleLargeDiameter": 6,
                    "installHoleSmallDiameter": 4, "installHoleMinimumWeb": 0.2}
        for key, value in expected.items():
            self.assertEqual(self.defaults[key], value)
        baseline = self.declare(dict(self.defaults))
        self.assertFalse(self.apertures(baseline))
        invalid_drafts = {**self.defaults, "installHoleCountPerSide": 1,
                          "installHoleOrientation": "invalid-unused-draft",
                          "installHoleTopOffset": -100, "installHoleBottomOffset": -100,
                          "installHoleLargeDiameter": -6, "installHoleSmallDiameter": -4,
                          "installHoleMinimumWeb": -1}
        self.assertEqual(self.declare(invalid_drafts), baseline)

    def test_three_pairs_have_inner_large_outer_small_coaxial_single_wall_cuts(self):
        values = self.values()
        layout, declaration, actual, _ = self.execute(values)
        self.assert_pairs(values, layout, declaration, [50, 625, 1200])
        instances = actual["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]
        records = {r["instanceId"]: r for r in instances}
        for process in self.apertures(declaration):
            record = records[process["key"]]
            self.assertTrue(record["result"]["applicable"])
            self.assertTrue(record["result"]["operations"])
            self.assertNotEqual(record["targetGeometry"], record["resultGeometry"])

    def test_holes_preserve_stock_identity_count_and_length_in_all_frame_styles(self):
        for frame_type in ("two_vertical", "closed_frame", "bottom_rail"):
            with self.subTest(frame_type=frame_type):
                values = self.values(frameType=frame_type)
                baseline, _, _, before = self.execute({**values, "installHoleEnabled": "no"})
                layout, declaration, _, after = self.execute(values)
                signature = lambda state: [(m["key"], m["role"], m["start"], m["end"])
                                           for m in state["members"]]
                self.assertEqual(signature(layout), signature(baseline))
                self.assertEqual(layout["connections"], baseline["connections"])
                for key in before:
                    self.assertEqual(after[key]["physicalLength"], before[key]["physicalLength"])
                    self.assertEqual(after[key]["axialInterval"], before[key]["axialInterval"])
                self.assert_pairs(values, layout, declaration, [50, 625, 1200])

    def test_four_and_five_groups_use_asymmetric_offsets_without_individual_adjustments(self):
        for count, centers in ((4, [50, 400, 750, 1100]),
                               (5, [50, 312.5, 575, 837.5, 1100])):
            with self.subTest(count=count):
                values = self.values(installHoleCountPerSide=count, installHoleBottomOffset=50,
                                     installHoleTopOffset=150)
                layout, declaration, _, _ = self.execute(values)
                self.assert_pairs(values, layout, declaration, centers)

    def test_valid_open_slot_maps_whole_side_count_to_real_segments(self):
        values = self.values(handleEnabled="yes", handleSide="left", handleClosure="open_slot",
                             installHoleCountPerSide=2)
        layout, declaration, _, _ = self.execute(values)
        self.assert_pairs(values, layout, declaration, [50, 1200])
        groups = layout["facts"]["installationHoles"]["groups"]
        self.assertEqual(len({g["memberKey"] for g in groups if g["side"] == "left"}), 2)
        self.assertEqual(len({g["memberKey"] for g in groups if g["side"] == "right"}), 1)

    def test_open_slot_rejects_missing_material_and_hole_crossing_a_cut_end(self):
        for side in ("left", "right"):
            with self.subTest(side=side, reason="center in absent upright"):
                self.assert_rejected(handleEnabled="yes", handleSide=side, handleClosure="open_slot")
            with self.subTest(side=side, reason="center exists but mouth crosses cut end"):
                self.assert_rejected(handleEnabled="yes", handleSide=side, handleClosure="open_slot",
                                     installHoleCountPerSide=2, installHoleBottomOffset=473.5)

    def test_default_ten_by_five_tube_cannot_take_six_mm_side_hole(self):
        self.assert_rejected(frameWidth=10, frameDepth=5, frameCornerRadius=1,
                             frameWallThickness=0.8)

    def test_front_holes_fit_default_section_and_allow_side_welds_at_same_station(self):
        values = self.values(installHoleOrientation="front", frameWidth=10, frameDepth=5,
                             frameCornerRadius=1, frameWallThickness=0.8,
                             firstBarTopOffset=55, lastBarBottomOffset=55,
                             installHoleTopOffset=55, installHoleBottomOffset=55)
        layout, declaration, _, _ = self.execute(values)
        # The first and last bores are intentionally at the same Z as welded
        # horizontal ends. Their front/back mouths do not touch the side welds.
        self.assert_pairs(values, layout, declaration, [55, 625, 1195])
        connections = {c["point"][2] for c in layout["connections"]}
        self.assertTrue({55, 1195} <= connections)

    def test_front_capacity_uses_actual_broad_face_of_snapshot(self):
        for radius, valid in ((1, True), (2, False)):
            with self.subTest(radius=radius), _profile_validation_scope(self.descriptor):
                source = self.values(installHoleEnabled="no", installHoleOrientation="front",
                                     frameWidth=10, frameDepth=5, frameCornerRadius=radius,
                                     frameWallThickness=0.8)
                profile = self.template.layout(source)["members"][0]["profile"]
                snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                            "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
                            "name": "正装真实宽面圆角", "specification": profile.specification,
                            "contentDigest": "installation-front-rounded-face-" + str(radius),
                            "width": profile.width, "depth": profile.depth,
                            "wallThickness": profile.wall, "contours": profile.contours()}
                values = {**source, "installHoleEnabled": "yes",
                          "tubeDesignerProfileOverrides": {"frame": snapshot}}
                if valid:
                    layout, declaration, _, _ = self.execute(values)
                    self.assertEqual(layout["members"][0]["profile"].radius, 0.0)
                    self.assert_pairs(values, layout, declaration, [50, 625, 1200])
                else:
                    with self.assertRaises(ValueError):
                        self.declare(values)

    def test_front_screw_path_accepts_short_tongue_but_rejects_long_tongue(self):
        changes = dict(installHoleOrientation="front", jointMode="male_female_half_hole",
                       innerWidth=20, innerDepth=10, maleHeadWidth=6,
                       installHoleBottomOffset=100)
        short = self.values(**changes, maleHeadLength=3)
        layout, declaration, _, _ = self.execute(short)
        self.assert_pairs(short, layout, declaration, [100, 650, 1200])
        # Left tongue reaches X=16..40 when length=24; the bore is at X=20.
        # Its front/back walls obstruct the screw's passage inside the A tube.
        self.assert_rejected(**changes, maleHeadLength=24)

    def test_exact_snapshot_rounding_controls_flat_face_capacity(self):
        for radius, valid in ((2, True), (12, False)):
            with self.subTest(radius=radius), _profile_validation_scope(self.descriptor):
                source = self.values(installHoleEnabled="no", frameCornerRadius=radius)
                profile = self.template.layout(source)["members"][0]["profile"]
                snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                            "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
                            "name": "真实侧壁圆角", "specification": profile.specification,
                            "contentDigest": "installation-rounded-face-" + str(radius),
                            "width": profile.width, "depth": profile.depth,
                            "wallThickness": profile.wall, "contours": profile.contours()}
                values = self.values(tubeDesignerProfileOverrides={"frame": snapshot})
                if valid:
                    layout, declaration, _, _ = self.execute(values)
                    self.assertEqual(layout["members"][0]["profile"].radius, 0.0)
                    self.assert_pairs(values, layout, declaration, [50, 625, 1200])
                else:
                    # The snapshot has no radius metadata. Its actual 6-mm flat
                    # side cannot contain a 6-mm opening plus retained material.
                    with self.assertRaises(ValueError):
                        self.declare(values)

    def test_false_snapshot_wall_metadata_cannot_select_cut_depth(self):
        with _profile_validation_scope(self.descriptor):
            source = self.values(installHoleEnabled="no")
            profile = self.template.layout(source)["members"][0]["profile"]
            snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                        "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
                        "name": "壁厚与真实轮廓不符", "specification": profile.specification,
                        "contentDigest": "installation-false-wall", "width": 40, "depth": 30,
                        "wallThickness": 0.8, "contours": profile.contours()}
            with self.assertRaises(ValueError):
                self.declare(self.values(tubeDesignerProfileOverrides={"frame": snapshot}))

    def test_end_margin_and_overlapping_mounting_holes_are_rejected(self):
        for changes in ({"installHoleBottomOffset": 3.1}, {"installHoleTopOffset": 3.1},
                        {"installHoleBottomOffset": 620, "installHoleTopOffset": 620},
                        {"installHoleBottomOffset": 700, "installHoleTopOffset": 700}):
            with self.subTest(changes=changes):
                self.assert_rejected(**changes)

    def test_count_and_bearing_diameter_contracts_are_enforced(self):
        for changes in ({"installHoleCountPerSide": 1}, {"installHoleCountPerSide": 6},
                        {"installHoleCountPerSide": 50},
                        {"installHoleCountPerSide": 2.5}, {"installHoleCountPerSide": True},
                        {"installHoleSmallDiameter": 6}, {"installHoleSmallDiameter": 7}):
            with self.subTest(changes=changes):
                self.assert_rejected(**changes)

    def test_mounting_mouth_cannot_overlap_a_welded_horizontal_end(self):
        self.assert_rejected(firstBarTopOffset=55, lastBarBottomOffset=55)

    def test_mounting_and_receiver_holes_share_one_aperture_chain_per_upright(self):
        values = self.values(jointMode="male_female_half_hole", innerWidth=20,
                             innerDepth=10, maleHeadLength=3, maleHeadWidth=6)
        baseline, _, _, before = self.execute({**values, "installHoleEnabled": "no"})
        layout, declaration, actual, after = self.execute(values)
        self.assert_pairs(values, layout, declaration, [50, 625, 1200])
        by_host = {}
        for process in self.apertures(declaration):
            call = process["definition"]
            host = call["targets"]["stock"]
            self.assertNotIn(host, by_host, "Receiver and installation holes must not restart separate stock chains")
            by_host[host] = process
            holes = call["processInput"]["geometry"]["holes"]
            self.assertEqual(sum("diameter" in h for h in holes), 6)
            self.assertEqual(sum("width" in h for h in holes), 2)
            self.assertTrue(call["dependencies"])
        self.assertEqual(set(by_host), {"frame.left.0001", "frame.right.0001"})
        for key in before:
            self.assertEqual(after[key]["physicalLength"], before[key]["physicalLength"])
        self.assertEqual(len(layout["members"]), len(baseline["members"]))
        executed = {r["instanceId"]: r for r in
                    actual["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]}
        for connection in declaration["connections"]:
            if connection["properties"]["recipe"] == "male_female_half_hole":
                for keys in connection["properties"]["manufacturingProcesses"].values():
                    self.assertTrue(all(key in executed for key in keys))

    def test_installation_hole_cannot_overlap_receiver_mouth_or_inserted_tongue(self):
        self.assert_rejected(jointMode="male_female_half_hole", innerWidth=20,
                             innerDepth=10, maleHeadLength=3, maleHeadWidth=6,
                             installHoleBottomOffset=100)


if __name__ == "__main__":
    unittest.main()
