import copy
import importlib.util
import math
from pathlib import Path
import unittest


RUNTIME = (Path(__file__).resolve().parents[5] / "apps" / "tube-designer"
           / "templates" / "_shared" / "assembly_template_runtime.py")


def load_runtime():
    spec = importlib.util.spec_from_file_location("assembly_template_runtime_t_insert", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TProfileInsertTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load_runtime()

    def preview(self, depth=12, gap=0.2, angle=90):
        return self.runtime.preview_plan("t-profile-insert",
            {"insertDepth": depth, "fitGap": gap},
            scene_parameters={"intersectionAngle": angle})

    def test_insert_is_an_independent_t_process_with_split_parameters(self):
        descriptor = self.runtime._template_by_id("t-profile-insert")
        visible = {item["id"] for item in self.runtime.catalogue()["assemblies"]}
        self.assertIn("t-profile-insert", visible)
        self.assertNotIn("two-end-middle", visible)
        self.assertEqual(descriptor["layoutShape"], "t")
        self.assertEqual({part["role"] for part in descriptor["previewScene"]["designParts"]},
                         {"host", "branch"})
        self.assertEqual({field["key"] for field in descriptor["parameters"]
                          if field.get("scope") == "scene"}, {"intersectionAngle"})
        self.assertEqual({field["key"] for field in descriptor["parameters"]
                          if field.get("scope") != "scene"}, {"insertDepth", "fitGap"})
        self.assertEqual([process["id"] for process in descriptor["partProcesses"]],
                         ["single-opening", "single-branch-end"])
        plan = self.preview()
        self.assertEqual(plan["parameters"], {"insertDepth": 12, "fitGap": 0.2})
        self.assertEqual(plan["sceneParameters"], {"intersectionAngle": 90})
        self.assertEqual(plan["sceneParts"]["branch"]["length"], 220)
        self.assertEqual(len(plan["manufacturingParts"]), 2)

    def test_axis_hole_and_insertion_depth_align_for_oblique_t(self):
        for angle in (60, 90, 120):
            with self.subTest(angle=angle):
                plan = self.preview(angle=angle)
                branch = next(item for item in plan["designParts"]
                              if item["role"] == "branch")
                host_blank = next(item for item in plan["manufacturingParts"]
                                  if item["sourceRole"] == "host")
                branch_blank = next(item for item in plan["manufacturingParts"]
                                    if item["sourceRole"] == "branch")
                feature, = host_blank["request"]["features"]
                axis = [branch["matrix"][0], branch["matrix"][4], branch["matrix"][8]]
                length = branch_blank["request"]["length"]
                tip = [branch["matrix"][index] + length * axis[axis_index]
                       for axis_index, index in enumerate((3, 7, 11))]
                entry = [tip[index] - 12 * axis[index] for index in range(3)]
                self.assertAlmostEqual(axis[0], -math.cos(math.radians(angle)))
                self.assertAlmostEqual(axis[1], 0)
                self.assertAlmostEqual(axis[2], -math.sin(math.radians(angle)))
                self.assertAlmostEqual(entry[2], 20)
                self.assertEqual(feature["toolRef"]["id"], "branch-profile")
                self.assertEqual(feature["angle"], angle)
                self.assertEqual(feature["azimuth"], 0)
                self.assertEqual(feature["direction"], "positive")
                self.assertEqual(feature["clearance"], 0.2)
                self.assertEqual(feature["section"]["profileRef"]["id"], "rect")

    def test_depth_changes_blank_and_hidden_penetration_without_moving_exterior(self):
        plans = [self.preview(depth=depth) for depth in (6, 12, 18)]
        branches = [next(item for item in plan["designParts"]
                         if item["role"] == "branch") for plan in plans]
        blanks = [next(item for item in plan["manufacturingParts"]
                       if item["sourceRole"] == "branch") for plan in plans]
        self.assertEqual([item["request"]["length"] for item in branches], [220, 220, 220])
        self.assertEqual([item["request"]["length"] for item in blanks], [226, 232, 238])
        self.assertEqual(branches, [branches[0]] * 3)
        self.assertEqual([item["matrix"][11] for item in branches], [240, 240, 240])
        self.assertEqual([plan["sceneParts"]["branch"]["length"] for plan in plans],
                         [220, 220, 220])
        self.assertEqual([branch["matrix"][11] - blank["request"]["length"]
                          for branch, blank in zip(branches, blanks)], [14, 8, 2])
        for plan in plans:
            host_blank = next(item for item in plan["manufacturingParts"]
                              if item["sourceRole"] == "host")
            self.assertEqual(len(host_blank["request"]["features"]), 1)
            self.assertTrue(all(check["status"] == "pass"
                                for check in plan["resolvedWorkflow"]["checks"]))
        self.assertTrue(plans[1]["resolvedWorkflow"]["parameterEffects"]
                        ["insertDepth"]["manufacturingGeometry"])

    def test_clearance_changes_hole_only(self):
        close = self.preview(gap=0.2)
        loose = self.preview(gap=0.8)
        self.assertEqual(close["designParts"], loose["designParts"])
        self.assertEqual(close["sceneParts"], loose["sceneParts"])
        holes = [next(item for item in plan["manufacturingParts"]
                      if item["sourceRole"] == "host")["request"]["features"][0]
                 for plan in (close, loose)]
        self.assertEqual([hole["clearance"] for hole in holes], [0.2, 0.8])
        for hole in holes:
            self.assertEqual(hole["section"]["parameters"]["width"], 40)

    def test_insertion_must_pass_near_wall_but_not_reach_far_wall(self):
        with self.assertRaises(ValueError):
            self.preview(depth=2)
        with self.assertRaises(ValueError):
            self.preview(depth=38)
        with self.assertRaises(ValueError):
            self.preview(depth=12, angle=10)

    def test_bound_trim_uses_insertion_depth_without_mutating_members(self):
        def section(width, depth):
            return {"schema": "icax.tube-profile", "schemaVersion": 1,
                    "id": "rect", "kind": "rect", "width": width, "depth": depth,
                    "parameters": {"width": width, "depth": depth, "wallThickness": 3},
                    "contours": [{"isHole": False, "points": [
                        [-width / 2, -depth / 2], [width / 2, -depth / 2],
                        [width / 2, depth / 2], [-width / 2, depth / 2]]}]}
        participants = [
            {"role": "host", "memberEntityId": "host-1", "itemKey": "host-key",
             "section": section(80, 40), "length": 440,
             "anchor": {"kind": "side", "face": "top", "reference": "start",
                        "station": 220, "offset": 0, "rotation": 0}},
            {"role": "branch", "memberEntityId": "branch-1", "itemKey": "branch-key",
             "section": section(30, 30), "length": 220,
             "anchor": {"kind": "end", "end": "start", "rotation": 0, "trim": 0,
                        "stockAllowance": 24, "contactInset": 20}},
        ]
        original = copy.deepcopy(participants)
        plans = [self.runtime.resolve_bound_plan("t-profile-insert",
                 {"insertDepth": depth, "fitGap": 0.2},
                 participants=participants, product_topology="T") for depth in (6, 12, 18)]
        self.assertEqual(participants, original)
        self.assertTrue(all(plan["supported"] for plan in plans))
        self.assertEqual([next(part for part in plan["parts"] if part["role"] == "branch")
                          ["ends"]["start"]["trim"] for plan in plans], [38, 32, 26])
        self.assertEqual([next(part for part in plan["parts"] if part["role"] == "host")
                          ["features"][0]["clearance"] for plan in plans], [0.2] * 3)
        without_contact = copy.deepcopy(participants)
        del without_contact[1]["anchor"]["contactInset"]
        with self.assertRaises(ValueError):
            self.runtime.resolve_bound_plan("t-profile-insert",
                {"insertDepth": 12, "fitGap": 0.2},
                participants=without_contact, product_topology="T")

    def test_oblique_bound_cutter_follows_branch_axis_and_entry_face(self):
        def section():
            return {"schema": "icax.tube-profile", "schemaVersion": 1,
                    "id": "rect", "kind": "rect", "width": 40, "depth": 40,
                    "parameters": {"width": 40, "depth": 40, "wallThickness": 2},
                    "contours": [{"isHole": False, "points": [
                        [-20, -20], [20, -20], [20, 20], [-20, 20]]}]}
        host = {"role": "host", "memberEntityId": "host-1", "itemKey": "host-key",
                "section": section(), "length": 440,
                "anchor": {"kind": "side", "face": "top", "reference": "start",
                           "station": 220, "offset": 0, "rotation": 0}}
        branch = {"role": "branch", "memberEntityId": "branch-1", "itemKey": "branch-key",
                  "section": section(), "length": 220,
                  "anchor": {"kind": "end", "end": "start", "rotation": 0, "trim": 0,
                             "stockAllowance": 24, "contactInset": 20}}
        angle = math.radians(60)
        branch["section"]["sectionFrame"] = {
            "hostFaceNormalToolPart": [0, 0, 1],
            "xAxisToolPart": [0, 1, 0],
            "yAxisToolPart": [-math.sin(angle), 0, math.cos(angle)]}
        values = {"intersectionAngle": 60, "insertDepth": 12, "fitGap": 0.2}
        plan = self.runtime.resolve_bound_plan("t-profile-insert", values,
                                              participants=[host, branch], product_topology="T")
        self.assertTrue(plan["supported"])
        opening = next(part for part in plan["parts"] if part["role"] == "host")["features"][0]
        self.assertEqual((opening["direction"], opening["angle"], opening["azimuth"]),
                         ("positive", 60, 0))
        wrong_axis = copy.deepcopy(branch)
        wrong_axis["section"]["sectionFrame"]["yAxisToolPart"] = [0, 0, 1]
        with self.assertRaises(ValueError):
            self.runtime.resolve_bound_plan("t-profile-insert", values,
                participants=[host, wrong_axis], product_topology="T")
        wrong_face = copy.deepcopy(branch)
        wrong_face["section"]["sectionFrame"]["hostFaceNormalToolPart"] = [0, 0, -1]
        with self.assertRaises(ValueError):
            self.runtime.resolve_bound_plan("t-profile-insert", values,
                participants=[host, wrong_face], product_topology="T")


if __name__ == "__main__":
    unittest.main()
