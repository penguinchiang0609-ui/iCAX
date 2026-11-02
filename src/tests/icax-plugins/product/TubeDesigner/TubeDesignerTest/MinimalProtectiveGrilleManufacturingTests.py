"""Real shared-executor checks for whole stock and frame-to-infill machining."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalProtectiveGrilleManufacturingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package("minimal_protective_grille")

    def execute(self, **changes):
        with _profile_validation_scope(self.descriptor):
            return self._execute(**changes)

    def _execute(self, **changes):
        values = {**self.defaults, **changes}
        before = deepcopy(values)
        layout = self.template.layout(values)
        design = self.template.display(values)
        with forbid_processing():
            definition = self.template.manufacturing(values)
        frozen_design, frozen_definition = deepcopy(design), deepcopy(definition)
        validate_manufacturing_model(definition)
        composed = compose_manufacturing_model(definition, design)
        source_keys = {item["key"] for item in composed["items"]
                       if "geometry" in item or "designInput" in item}
        self.assertEqual(source_keys, {value["itemKey"] for value in composed["sourceMappings"]})
        actual = execute_definition(definition, design=design)
        self.assertEqual(values, before)
        self.assertEqual(design, frozen_design)
        self.assertEqual(definition, frozen_definition)
        preparations = {entry["itemKey"]: entry for entry in
                        actual["extensions"]["tubeDesigner.workpiecePreparations"]}
        self.assertEqual(set(preparations), {item["key"] for item in layout["members"]})
        self.assertTrue(all(entry["ready"] and entry["status"] == "prepared" for entry in preparations.values()))
        return layout, definition, actual, preparations

    def test_butt_weld_preserves_every_whole_stock_for_all_styles(self):
        for frame_type in ("two_vertical", "closed_frame", "bottom_rail"):
            for side in ("left", "right", "center_double"):
                for closure in ("closed", "open_slot"):
                    with self.subTest(frame_type=frame_type, side=side, closure=closure):
                        layout, declaration, actual, preparations = self.execute(
                            frameType=frame_type, handleSide=side, handleClosure=closure)
                        self.assertEqual(len(declaration["processes"]), len(layout["members"]))
                        self.assertTrue(all(connection["properties"]["recipe"] == "butt_weld"
                                            for connection in declaration["connections"]))
                        for item in layout["members"]:
                            self.assertAlmostEqual(preparations[item["key"]]["physicalLength"],
                                                   math.dist(item["start"], item["end"]), places=8)
                        self.assertFalse(actual["extensions"].get("tubeDesigner.assemblyGeometryProcesses"))

    def test_small_equal_tubes_declare_corner_crossing_half_holes(self):
        # Exact native topology, not a flat-width heuristic, decides whether
        # the receiving stock remains connected after these openings.
        self.execute(jointMode="male_female_half_hole")

    def test_mixed_connections_extend_only_actual_frame_inserted_ends(self):
        for side in ("left", "right", "center_double"):
            with self.subTest(side=side):
                layout, definition, actual, preparations = self.execute(
                    jointMode="male_female_half_hole", frameWidth=40, frameDepth=30,
                    frameCornerRadius=2, innerWidth=20, innerDepth=10, handleSide=side)
                members = {item["key"]: item for item in layout["members"]}
                inserted = {key: set() for key in members}
                for connection in definition["connections"]:
                    host_key, branch_key = connection["items"]
                    recipe = connection["properties"]["recipe"]
                    if recipe == "male_female_half_hole":
                        self.assertTrue(members[host_key]["role"].startswith("frame."))
                        self.assertTrue(members[branch_key]["role"].startswith("inner."))
                        self.assertEqual(members[branch_key]["start"][2], members[branch_key]["end"][2])
                        inserted[branch_key].add(connection["properties"]["branchEnd"])
                    else:
                        self.assertEqual(recipe, "butt_weld")
                for key, member in members.items():
                    prep = preparations[key]
                    base = math.dist(member["start"], member["end"])
                    self.assertAlmostEqual(prep["physicalLength"], base + len(inserted[key]) * 2.0, places=8)
                    interval = prep["axialInterval"]
                    self.assertAlmostEqual(interval["startStation"], -2 if "start" in inserted[key] else 0)
                    self.assertAlmostEqual(interval["endStation"], base + (2 if "end" in inserted[key] else 0))
                self.assertTrue(any(len(ends) == 1 for ends in inserted.values()))
                records = actual["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"]
                self.assertTrue(records)
                self.assertTrue(all(record["result"]["applicable"] for record in records))
                executed = {(record["instanceId"], record["stockId"]): record for record in records}
                declared = {process["key"]: process["definition"] for process in definition["processes"]}
                for connection in definition["connections"]:
                    if connection["kind"] == "weld":
                        self.assertNotIn("manufacturingProcesses", connection["properties"])
                        continue
                    bindings = connection["properties"]["manufacturingProcesses"]
                    self.assertEqual(set(bindings), set(connection["items"]))
                    for member_key, process_keys in bindings.items():
                        self.assertTrue(process_keys)
                        for process_key in process_keys:
                            self.assertIn(member_key, declared[process_key]["targets"].values())
                            record = executed[process_key, member_key]
                            self.assertTrue(record["result"]["operations"])
                            self.assertNotEqual(record["resultGeometry"], record["targetGeometry"])

    def test_half_holes_cut_only_the_entering_wall(self):
        layout, definition, _, _ = self.execute(jointMode="male_female_half_hole",
            frameWidth=40, frameDepth=30, frameCornerRadius=2, innerWidth=20, innerDepth=10)
        members = {item["key"]: item for item in layout["members"]}
        holes = [p for p in definition["processes"] if p["definition"]["templateId"] == "structural-apertures"]
        self.assertEqual(len(holes), 2)
        for process in holes:
            call = process["definition"]
            host = members[call["targets"]["stock"]]
            for hole in call["processInput"]["geometry"]["holes"]:
                self.assertAlmostEqual(math.hypot(*hole["vector"]), host["profile"].width/2 + 2.e-7)
                self.assertAlmostEqual(hole["width"], 10.1)
                self.assertAlmostEqual(hole["height"], 2.1)
                self.assertEqual(hole["radius"], 0)

    def test_inactive_machining_drafts_do_not_affect_butt_weld(self):
        _, baseline, _, _ = self.execute()
        _, changed, _, _ = self.execute(maleHeadLength=-100, maleHeadWidth=-100,
                                       maleCornerType="invalid", minimumFeatureWeb=-100)
        self.assertEqual(baseline, changed)

    def test_frame_corners_and_handle_jambs_stay_butt_weld(self):
        layout, definition, _, _ = self.execute(jointMode="male_female_half_hole",
            frameType="closed_frame", frameWidth=40, frameDepth=30,
            frameCornerRadius=2, innerWidth=20, innerDepth=10)
        members = {item["key"]: item for item in layout["members"]}
        for connection in definition["connections"]:
            host, branch = (members[key] for key in connection["items"])
            if branch["role"].startswith("frame.") or host["role"].startswith("inner."):
                self.assertEqual(connection["properties"]["recipe"], "butt_weld")

    def test_round_and_chamfer_are_executed_without_silent_size_clamping(self):
        for corner in ("round", "chamfer"):
            with self.subTest(corner=corner):
                self.execute(jointMode="male_female_half_hole", frameWidth=40, frameDepth=30,
                    frameCornerRadius=2, innerWidth=20, innerDepth=10,
                    maleCornerType=corner, maleCornerSize=0.5)
                with self.assertRaisesRegex(ValueError, "端角尺寸"):
                    self.template.manufacturing({**self.defaults, "jointMode": "male_female_half_hole",
                        "maleCornerType": corner, "maleCornerSize": 2})

    def test_profile_snapshots_do_not_reject_corner_crossing_openings(self):
        changes = dict(jointMode="male_female_half_hole", frameWidth=40, frameDepth=30,
                       frameCornerRadius=2, innerWidth=20, innerDepth=10)
        for radius in (2, 12):
            source_values = {**self.defaults, **changes, "frameCornerRadius": radius}
            profile = next(member["profile"] for member in self.template.layout(source_values)["members"]
                           if member["role"] == "frame.left")
            profile_values = deepcopy(profile._profile_data['_familyParameters'])
            profile_values['innerRadius'] = max(0., radius-profile.wall)
            coherent_contours = profile._module.contours(profile._module.build(profile_values))
            snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
                "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
                "name": "圆角方矩管快照", "specification": profile.specification,
                "contentDigest": "rounded-face-test-" + str(radius), "width": profile.width,
                "depth": profile.depth, "wallThickness": profile.wall, "contours": coherent_contours}
            override = {"frame": snapshot}
            with self.subTest(radius=radius), _profile_validation_scope(self.descriptor):
                values = {**self.defaults, **changes, "tubeDesignerProfileOverrides": override}
                actual_profile = next(member["profile"] for member in self.template.layout(values)["members"]
                                      if member["role"] == "frame.left")
                self.assertEqual(actual_profile.radius, 0.0)
                self.execute(**changes, tubeDesignerProfileOverrides=override)

    def test_false_wall_metadata_cannot_authorize_a_snapshot_cut(self):
        values = {**self.defaults, "frameWidth": 40, "frameDepth": 30, "frameCornerRadius": 2}
        profile = self.template.layout(values)["members"][0]["profile"]
        snapshot = {"schema": "icax.imported-tube-profile", "schemaVersion": 1,
            "kind": "profile-package", "profileForm": "parametric", "sectionKind": "rect",
            "name": "壁厚元数据与边界不符", "specification": profile.specification,
            "contentDigest": "incorrect-wall-test", "width": 40, "depth": 30,
            "wallThickness": 1.1, "contours": profile.contours()}
        with _profile_validation_scope(self.descriptor), self.assertRaisesRegex(ValueError, "壁厚参数不一致"):
            self.template.manufacturing({**values, "jointMode": "male_female_half_hole",
                "innerWidth": 20, "innerDepth": 10, "tubeDesignerProfileOverrides": {"frame": snapshot}})


if __name__ == "__main__":
    unittest.main()
