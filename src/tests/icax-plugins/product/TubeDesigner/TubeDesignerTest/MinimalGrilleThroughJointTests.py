"""Through joints keep whole stock and reach the actual far receiver surface.

These independent declaration/executor tests complement the real native STEP
readback in output/tests/minimal-grille-through-joint-20261027.
"""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package, public_preview
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model
from icax_template_sdk.profile_constraints import _profile_validation_scope


THROUGH = "male_female_through_hole"
HALF = "male_female_half_hole"


class MinimalGrilleThroughJointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package("minimal_protective_grille")

    def values(self, **changes):
        return {**self.defaults, "jointMode": THROUGH, "frameWidth": 40, "frameDepth": 30,
                "frameCornerRadius": 2, "frameWallThickness": 1.5,
                "innerWidth": 20, "innerDepth": 10, "innerCornerRadius": 1,
                "innerWallThickness": 1, "maleHeadLength": 3, "maleHeadWidth": 6,
                "maleCornerType": "square", "maleCornerSize": .5,
                "holeTotalClearanceDepth": .1, "holeTotalClearanceHeight": .1,
                "minimumFeatureWeb": .2, "handleEnabled": "no",
                "barLayoutMode": "fixed_count", "barCount": 2,
                "firstBarTopOffset": 100, "lastBarBottomOffset": 100,
                "installHoleEnabled": "no", **changes}

    def execute(self, values):
        frozen = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            layout, design = self.template.layout(values), self.template.display(values)
            with forbid_processing():
                declaration = self.template.manufacturing(values)
            frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
            validate_manufacturing_model(declaration)
            composed = compose_manufacturing_model(declaration, design)
            self.assertEqual({m["itemKey"] for m in composed["sourceMappings"]},
                             {m["key"] for m in layout["members"]})
            executed = execute_definition(declaration, design=design)
        self.assertEqual(values, frozen)
        self.assertEqual(design, frozen_design)
        self.assertEqual(declaration, frozen_declaration)
        prepared = {p["itemKey"]: p for p in executed["extensions"]["tubeDesigner.workpiecePreparations"]}
        self.assertEqual(set(prepared), {m["key"] for m in layout["members"]})
        self.assertTrue(all(p["ready"] and p["status"] == "prepared" for p in prepared.values()))
        return layout, declaration, executed, prepared

    @staticmethod
    def apertures(declaration):
        return {p["definition"]["targets"]["stock"]: p["definition"]["processInput"]["geometry"]["holes"]
                for p in declaration["processes"] if p["definition"]["templateId"] == "structural-apertures"}

    def reject(self, **changes):
        values = self.values(**changes)
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor), forbid_processing(), self.assertRaises(ValueError):
            self.template.manufacturing(values)
        self.assertEqual(values, before)

    def check_actual_extensions(self, values):
        layout, declaration, executed, prepared = self.execute(values)
        members = {m["key"]: m for m in layout["members"]}
        extensions = {key: {} for key in members}
        for connection in declaration["connections"]:
            props = connection["properties"]
            host, branch = (members[key] for key in connection["items"])
            eligible = host["role"].startswith("frame.") and branch["role"].startswith("inner.") and abs(branch["start"][2]-branch["end"][2]) < 1.e-7
            self.assertEqual(props["recipe"], THROUGH if eligible else "butt_weld")
            if not eligible:
                continue
            extension = host["profile"].width
            self.assertFalse(props["halfHole"])
            self.assertTrue(props["throughHole"])
            self.assertTrue(props["flushWithOppositeOuterFace"])
            self.assertAlmostEqual(props["insertionDepth"], extension)
            extensions[branch["key"]][props["branchEnd"]] = extension
        for key, member in members.items():
            visible = math.dist(member["start"], member["end"])
            start = extensions[key].get("start", 0)
            end = extensions[key].get("end", 0)
            self.assertAlmostEqual(prepared[key]["physicalLength"], visible+start+end, places=8)
            self.assertAlmostEqual(prepared[key]["axialInterval"]["startStation"], -start)
            self.assertAlmostEqual(prepared[key]["axialInterval"]["endStation"], visible+end)
        instances = executed["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]
        self.assertTrue(instances)
        self.assertTrue(all(item["result"]["applicable"] and item["result"]["operations"]
                            and item["resultGeometry"] != item["targetGeometry"] for item in instances))
        return layout, declaration, prepared, extensions

    def test_new_enum_public_preview_preserves_complete_inputs(self):
        field = next(p for p in self.descriptor["parameters"] if p["key"] == "jointMode")
        self.assertIn(THROUGH, {choice["value"] for choice in field["choices"]})
        values = self.values(maleHeadLength=7)
        before = deepcopy(values)
        for purpose in ("display", "manufacturing"):
            result = public_preview("minimal_protective_grille", self.descriptor, values, purpose)
            self.assertEqual(values, before)
            self.assertNotIn("parameters", result)
            self.assertNotIn("derivedMaleHeadLength", values)

    def test_half_and_through_differ_in_real_wall_cut_count_and_stock_extension(self):
        half_layout, half, _, half_prepared = self.execute(self.values(jointMode=HALF))
        layout, through, prepared, extensions = self.check_actual_extensions(self.values())
        self.assertEqual([(m["key"], m["start"], m["end"]) for m in layout["members"]],
                         [(m["key"], m["start"], m["end"]) for m in half_layout["members"]])
        half_holes, through_holes = self.apertures(half), self.apertures(through)
        self.assertEqual(set(half_holes), set(through_holes))
        for key in half_holes:
            self.assertEqual(len(through_holes[key]), 2*len(half_holes[key]))
        for key, ends in extensions.items():
            self.assertAlmostEqual(prepared[key]["physicalLength"]-half_prepared[key]["physicalLength"],
                                   len(ends)*(40-3), places=8)

    def test_rectangular_mother_openings_are_two_separate_full_wall_cuts(self):
        values = self.values()
        layout, declaration, _, _ = self.check_actual_extensions(values)
        members = {m["key"]: m for m in layout["members"]}
        apertures = self.apertures(declaration)
        count = 0
        for connection in declaration["connections"]:
            props = connection["properties"]
            if props["recipe"] != THROUGH:
                continue
            host_key, branch_key = connection["items"]
            host, branch = members[host_key], members[branch_key]
            point, normal = props["contactPoint"], props["faceNormal"]
            holes = [h for h in apertures[host_key] if abs(h["placement"]["origin"][2]-point[2]) < 1.e-7]
            self.assertEqual(len(holes), 2)
            for offset, outward in ((0, 1), (host["profile"].width, -1)):
                expected = [point[i]-offset*normal[i]+outward*normal[i]*1.e-7 for i in range(3)]
                hole = min(holes, key=lambda h: math.dist(h["placement"]["origin"], expected))
                self.assertLess(math.dist(hole["placement"]["origin"], expected), 1.e-8)
                self.assertAlmostEqual(hole["width"], branch["profile"].depth+.1)
                self.assertAlmostEqual(hole["height"], 6.1)
                self.assertEqual(hole["radius"], 0)
                self.assertAlmostEqual(math.hypot(*hole["vector"]), host["profile"].width/2+2.e-7, places=8)
                self.assertAlmostEqual(sum(v*n for v, n in zip(hole["vector"], normal)),
                                       -outward*(host["profile"].width/2+2.e-7), places=8)
            count += 1
        self.assertEqual(count, 4)

    def test_two_ended_heads_extend_to_the_two_outer_frame_faces(self):
        values = self.values()
        layout, _, prepared, extensions = self.check_actual_extensions(values)
        bars = [m for m in layout["members"] if len(extensions[m["key"]]) == 2]
        self.assertEqual(len(bars), 2)
        for bar in bars:
            self.assertAlmostEqual(prepared[bar["key"]]["physicalLength"], values["width"])
            self.assertEqual(bar["start"][0]-extensions[bar["key"]]["start"], 0)
            self.assertEqual(bar["end"][0]+extensions[bar["key"]]["end"], values["width"])

    def test_one_ended_heads_leave_handle_and_center_jamb_ends_unextended(self):
        for side in ("left", "right", "center_double"):
            for closure in ("closed", "open_slot"):
                with self.subTest(side=side, closure=closure):
                    _, _, _, extensions = self.check_actual_extensions(self.values(
                        handleEnabled="yes", handleSide=side, handleClosure=closure))
                    self.assertTrue(any(len(ends) == 1 for ends in extensions.values()))
                    self.assertTrue(any(len(ends) == 0 for ends in extensions.values()))

    def test_derived_head_length_tracks_actual_receiver_width(self):
        for width in (32, 48):
            with self.subTest(width=width):
                _, _, _, extensions = self.check_actual_extensions(self.values(frameWidth=width, maleHeadLength=99))
                self.assertTrue(all(length == width for ends in extensions.values() for length in ends.values()))

    def test_profile_snapshot_actual_width_overrides_nominal_draft_width(self):
        values = self.values(frameWidth=48)
        profile = next(m["profile"] for m in self.template.layout(values)["members"] if m["role"] == "frame.left")
        snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
            "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
            "name": "真实宽度48的母管", "specification": profile.specification,
            "contentDigest": "through-width48-independent-test", "width": profile.width,
            "depth": profile.depth, "wallThickness": profile.wall, "contours": profile.contours()}
        _, _, _, extensions = self.check_actual_extensions(self.values(frameWidth=40,
            tubeDesignerProfileOverrides={"frame": snapshot}))
        self.assertTrue(all(length == 48 for ends in extensions.values() for length in ends.values()))

    def test_through_allows_far_corner_when_entering_wall_is_flat(self):
        def contour(width, depth, left_radius, right_radius):
            x, y, left, right = width/2, depth/2, left_radius, right_radius
            q = 1-math.sqrt(.5)
            points = [(-x,y-left),(-x,-y+left),(-x+left,-y),(x-right,-y),
                      (x,-y+right),(x,y-right),(x-right,y),(-x+left,y)]
            middles = {1:(-x+left*q,-y+left*q),3:(x-right*q,-y+right*q),
                       5:(x-right*q,y-right*q),7:(-x+left*q,y-left*q)}
            segments = []
            for index, start in enumerate(points):
                segment = {"kind": "arc" if index in middles else "line", "start": list(start),
                           "end": list(points[(index+1)%len(points)])}
                if index in middles:
                    segment["middle"] = list(middles[index])
                segments.append(segment)
            return {"kind": "path", "closed": True, "segments": segments}
        snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
            "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
            "name": "近壁宽平面远壁窄平面", "specification": "40x30 asymmetric corners",
            "contentDigest": "through-asymmetric-wall-test", "width": 40, "depth": 30,
            "wallThickness": 1.5,
            "contours": [contour(40,30,12,2), contour(37,27,10.5,.5)]}
        with _profile_validation_scope(self.descriptor):
            custom = next(m["profile"] for m in self.template.layout(self.values(
                tubeDesignerProfileOverrides={"frame": snapshot}))["members"] if m["role"] == "frame.left")
            half_values = self.values(jointMode=HALF)
            layout = self.template.layout(half_values)
            next(m for m in layout["members"] if m["role"] == "frame.left")["profile"] = custom
            rules = self.template._load(self.template.ROOT / "manufacturing_rules.py")
            with forbid_processing():
                half = rules.manufacturing(half_values, layout)
            self.assertTrue(half["processes"])
            with forbid_processing():
                through = rules.manufacturing({**half_values, "jointMode": THROUGH}, layout)
            self.assertTrue(through['processes'])

    def test_inactive_single_wall_length_drafts_do_not_change_through_manufacturing(self):
        _, baseline, _, _ = self.execute(self.values())
        for draft in (-999, 0, 1, 99, 1000000):
            with self.subTest(draft=draft):
                _, changed, _, _ = self.execute(self.values(maleHeadLength=draft))
                self.assertEqual(changed, baseline)

    def test_round_and_chamfer_use_receiver_derived_length_without_clamping(self):
        for corner in ("round", "chamfer"):
            with self.subTest(corner=corner):
                self.check_actual_extensions(self.values(maleCornerType=corner, maleCornerSize=.5, maleHeadLength=.1))
                self.reject(maleCornerType=corner, maleCornerSize=3)

    def test_head_shoulders_and_valid_parameter_values_are_required(self):
        invalid = ({"maleHeadWidth": 2}, {"maleHeadWidth": 19.8},
                   {"minimumFeatureWeb": 0},
                   {"holeTotalClearanceDepth": -1}, {"holeTotalClearanceHeight": -1})
        for changes in invalid:
            with self.subTest(changes=changes):
                self.reject(**changes)

    def test_receiver_opening_sizes_are_deferred_to_native_final_connectivity(self):
        for changes in ({'maleHeadWidth':19, 'holeTotalClearanceHeight':1},
                        {'innerDepth':28}, {'holeTotalClearanceDepth':50},
                        {'holeTotalClearanceHeight':60}):
            with self.subTest(changes=changes):
                self.execute(self.values(**changes))

    def test_receiver_end_and_overlapping_joint_positions_are_rejected(self):
        for changes in ({"firstBarTopOffset": 2}, {"lastBarBottomOffset": 2},
                        {"height": 250, "barCount": 20}):
            with self.subTest(changes=changes):
                self.reject(**changes)

    def test_opposed_heads_cannot_share_the_same_receiver_cavity(self):
        values = self.values(jointMode=HALF)
        with _profile_validation_scope(self.descriptor):
            layout = self.template.layout(values)
            contact = next(c for c in layout["connections"] if c["hostKey"].startswith("frame.left."))
            source = next(m for m in layout["members"] if m["key"] == contact["branchKey"])
            opposed = {**source, "key": "inner.opposed.test", "start": (-100., 0., source["start"][2]),
                       "end": (0., 0., source["start"][2])}
            layout["members"].append(opposed)
            layout["connections"].append({**contact, "key": "contact.opposed.test", "branchKey": opposed["key"],
                "branchEnd": "end", "point": opposed["end"], "normal": (-1., 0., 0.)})
            rules = self.template._load(self.template.ROOT / "manufacturing_rules.py")
            with forbid_processing():
                half = rules.manufacturing(values, layout)
            self.assertTrue(half["connections"])
            # Through heads also claim the same two receiver wall openings;
            # either aperture overlap or tongue collision must reject them.
            with forbid_processing(), self.assertRaises(ValueError):
                rules.manufacturing({**values, "jointMode": THROUGH}, layout)

    def test_one_physical_end_cannot_receive_two_joint_assignments(self):
        values = self.values()
        with _profile_validation_scope(self.descriptor):
            layout = self.template.layout(values)
            layout["connections"].append({**layout["connections"][0], "key": "contact.duplicate-end.test"})
            rules = self.template._load(self.template.ROOT / "manufacturing_rules.py")
            with forbid_processing(), self.assertRaisesRegex(ValueError, "同一管材端部"):
                rules.manufacturing(values, layout)

    def test_front_screw_channel_rejects_through_head_but_allows_short_half_head(self):
        changes = {"installHoleEnabled": "yes", "installHoleOrientation": "front",
                   "installHoleCountPerSide": 2, "installHoleBottomOffset": 100,
                   "installHoleTopOffset": 100}
        self.execute(self.values(**changes, jointMode=HALF, maleHeadLength=3))
        for inactive_length in (-999, 3, 99):
            with self.subTest(inactive_length=inactive_length):
                self.reject(**changes, maleHeadLength=inactive_length)

    def test_mounting_holes_share_one_aperture_chain_when_clear_and_reject_overlap(self):
        changes = {"installHoleEnabled": "yes", "installHoleCountPerSide": 3,
                   "installHoleBottomOffset": 50, "installHoleTopOffset": 50}
        for orientation in ("side", "front"):
            with self.subTest(orientation=orientation):
                _, declaration, _, _ = self.execute(self.values(**changes, installHoleOrientation=orientation))
                calls = [p for p in declaration["processes"] if p["definition"]["templateId"] == "structural-apertures"]
                self.assertEqual(len(calls), 2)
                for holes in self.apertures(declaration).values():
                    self.assertEqual(sum("diameter" in h for h in holes), 6)
                    self.assertEqual(sum("width" in h for h in holes), 4)
        self.reject(installHoleEnabled="yes", installHoleOrientation="side", installHoleCountPerSide=2,
                    installHoleBottomOffset=100, installHoleTopOffset=100)

    def test_through_joints_do_not_split_continuous_rows_or_frame_members(self):
        for frame_type in ("two_vertical", "closed_frame", "bottom_rail"):
            with self.subTest(frame_type=frame_type):
                values = self.values(frameType=frame_type, handleEnabled="yes", handleSide="left")
                off, _, _, off_prepared = self.execute({**values, "jointMode": "butt_weld"})
                on, _, _, on_prepared = self.execute(values)
                signature = lambda state: [(m["key"], m["role"], m["start"], m["end"]) for m in state["members"]]
                self.assertEqual(signature(on), signature(off))
                self.assertEqual(on["connections"], off["connections"])
                self.assertEqual(set(on_prepared), set(off_prepared))

    def test_disabled_joint_and_mounting_fields_do_not_affect_butt_weld(self):
        values = self.values(jointMode="butt_weld")
        _, baseline, _, _ = self.execute(values)
        _, changed, _, _ = self.execute({**values, "maleHeadLength": -999, "maleHeadWidth": -999,
            "maleCornerType": "invalid-unused", "maleCornerSize": -999, "minimumFeatureWeb": -999,
            "holeTotalClearanceDepth": -999, "holeTotalClearanceHeight": -999,
            "installHoleOrientation": "invalid-unused", "installHoleCountPerSide": -999,
            "installHoleLargeDiameter": -999, "installHoleSmallDiameter": -999})
        self.assertEqual(baseline, changed)


if __name__ == "__main__":
    unittest.main()
