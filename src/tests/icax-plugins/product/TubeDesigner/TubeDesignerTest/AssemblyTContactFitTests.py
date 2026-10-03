import copy
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5] / "apps" / "tube-designer" / "templates"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TContactFitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assembly = load(ROOT / "_shared" / "assembly_template_runtime.py", "t_contact_assembly")
        cls.tool = load(ROOT / "_shared" / "punch_tool_runtime.py", "t_contact_tool")
        cls.rect = load(ROOT / "profile" / "rect" / "profile.py", "t_contact_rect")

    @staticmethod
    def section(width=40, depth=40):
        return {"schema": "icax.tube-profile", "schemaVersion": 1,
                "id": "rect", "kind": "rect", "width": width, "depth": depth,
                "parameters": {"width": width, "depth": depth, "wallThickness": 2},
                "contours": [{"kind": "roundedRectangle", "center": [0, 0],
                              "width": width, "height": depth, "radius": 3},
                             {"kind": "roundedRectangle", "center": [0, 0],
                              "width": width - 4, "height": depth - 4, "radius": 1}]}

    def test_independent_template_and_continuous_t_shape(self):
        catalogue = self.assembly.catalogue()
        self.assertIn("t-contact-fit", {entry["id"] for entry in catalogue["assemblies"]})
        self.assertNotIn("two-end-middle", {entry["id"] for entry in catalogue["assemblies"]})
        plain = self.assembly.preview_plan("t-contact-fit", {"markContact": False})
        marked = self.assembly.preview_plan("t-contact-fit", {"markContact": True})
        self.assertEqual(plain["sceneParameters"], {"intersectionAngle": 90})
        self.assertEqual(plain["designParts"], marked["designParts"])
        self.assertEqual(plain["sceneParts"], marked["sceneParts"])
        host = next(part for part in plain["designParts"] if part["role"] == "host")
        branch = next(part for part in plain["designParts"] if part["role"] == "branch")
        self.assertEqual([host["matrix"][0], host["matrix"][4], host["matrix"][8]], [1, 0, 0])
        for actual, expected in zip([branch["matrix"][0], branch["matrix"][4], branch["matrix"][8]], [0, 0, -1]):
            self.assertAlmostEqual(actual, expected, places=9)
        self.assertAlmostEqual(branch["matrix"][11] - branch["request"]["length"], 20)
        self.assertEqual(sum(len(part["request"]["features"]) for part in plain["manufacturingParts"]), 0)
        host_blank = next(part for part in marked["manufacturingParts"] if part["sourceRole"] == "host")
        feature, = host_blank["request"]["features"]
        self.assertEqual(feature["toolRef"]["id"], "contact-outline-mark")
        self.assertEqual(feature["face"], "top")
        self.assertTrue(feature["blindHole"])
        self.assertEqual(feature["cutDepth"], 0.2)
        self.assertEqual(feature["section"]["profileRef"]["id"], "rect")
        oblique_product = self.assembly.finished_products.create("t")
        oblique_product["parameters"]["intersectionAngle"] = 75
        with self.assertRaises(ValueError):
            self.assembly.preview_plan("t-contact-fit", finished_product=oblique_product)

    def test_real_tool_produces_closed_narrow_ring_for_branch_section(self):
        section = self.section()
        snapshot = self.tool._evaluate(
            {"id": "contact-outline-mark"}, {"markMode": "contour", "lineWidth": 0.6},
            {"target": "side", "section": {"profile": section}})
        self.assertEqual(snapshot["resolution"], "installed")
        self.assertEqual(snapshot["geometry"]["mode"], "profile")
        outer, inner = snapshot["geometry"]["contours"]
        self.assertEqual(outer["width"], 40)
        self.assertAlmostEqual(inner["width"], 38.8)
        self.assertAlmostEqual(inner["height"], 38.8)
        self.assertAlmostEqual(inner["radius"], 2.4)
        with self.assertRaises(ValueError):
            self.tool._evaluate({"id": "contact-outline-mark"},
                                {"markMode": "unsupported", "lineWidth": 0.6},
                                {"target": "side", "section": {"profile": section}})

    def test_actual_member_binding_is_connection_only_or_shallow_cut(self):
        host_section = self.section(80, 40)
        host_section["sectionFrame"] = {"verification": "committed-source-brep-replay"}
        branch_section = self.section()
        branch_section["sectionFrame"] = {
            "verification": "committed-source-brep-replay",
            "xAxisToolPart": [1, 0, 0],
            "yAxisToolPart": [0, -1, 0],
            "hostFaceNormalToolPart": [0, 0, 1],
        }
        participants = [
            {"role": "host", "memberEntityId": "host-1", "itemKey": "host", "section": host_section,
             "length": 440, "anchor": {"kind": "side", "face": "top", "reference": "start",
                                        "station": 220, "offset": 0, "rotation": 0}},
            {"role": "branch", "memberEntityId": "branch-1", "itemKey": "branch", "section": branch_section,
             "length": 220, "anchor": {"kind": "end", "end": "end", "rotation": 0,
                                         "trim": 0, "stockAllowance": 0, "contactInset": 20},
             "productAnchor": {"kind": "end", "end": "end"}},
        ]
        original = copy.deepcopy(participants)
        plain = self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": False},
                                                 participants=participants, product_topology="T")
        marked = self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": True},
                                                  participants=participants, product_topology="T")
        self.assertEqual(participants, original)
        self.assertTrue(plain["supported"] and marked["supported"])
        self.assertEqual(plain["manufacturingEffect"], "incremental-cut")
        self.assertEqual(plain["fitChecks"][0]["kind"], "end-side-flat-contact")
        self.assertEqual(plain["fitChecks"][0]["processIds"], ["branch-butt-end"])
        branch_end, = next(item for item in plain["parts"] if item["role"] == "branch")["ends"].values()
        self.assertAlmostEqual(branch_end["trim"], 20)
        self.assertFalse(any(item["role"] == "host" for item in plain["parts"]))
        self.assertEqual(marked["manufacturingEffect"], "incremental-cut")
        feature, = marked["parts"][0]["features"]
        self.assertEqual(feature["face"], "top")
        self.assertEqual(feature["section"]["profile"]["width"], 40)
        self.assertTrue(feature["blindHole"])
        self.assertAlmostEqual(feature["cutDepth"], 0.2)

        round_host = copy.deepcopy(participants)
        round_host[0]["section"]["contours"][0] = {"kind": "circle", "center": [0, 0], "radius": 40}
        with self.assertRaisesRegex(ValueError, "平面方矩管|平直"):
            self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": False},
                                             participants=round_host, product_topology="T")
        tilted = copy.deepcopy(participants)
        tilted[0]["anchor"]["station"] = 10
        with self.assertRaisesRegex(ValueError, "超出主管平直接触面"):
            self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": False},
                                             participants=tilted, product_topology="T")

        missing_frame = copy.deepcopy(participants)
        del missing_frame[1]["section"]["sectionFrame"]
        with self.assertRaisesRegex(ValueError, "已核对的真实制造截面"):
            self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": False},
                                             participants=missing_frame, product_topology="T")
        reversed_end = copy.deepcopy(participants)
        reversed_end[1]["productAnchor"]["end"] = "start"
        with self.assertRaisesRegex(ValueError, "没有沿主管所选接触面的法向向外伸出"):
            self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": False},
                                             participants=reversed_end, product_topology="T")
        wrong_face = copy.deepcopy(participants)
        wrong_face[1]["section"]["sectionFrame"]["hostFaceNormalToolPart"] = [0, 0, -1]
        with self.assertRaisesRegex(ValueError, "没有沿主管所选接触面的法向向外伸出"):
            self.assembly.resolve_bound_plan("t-contact-fit", {"markContact": False},
                                             participants=wrong_face, product_topology="T")

    def test_t_processes_share_one_finished_scene_at_right_angle(self):
        ids = ("t-contact-fit", "end-side-tab-slot", "t-profile-insert")
        product = self.assembly.get_example_product("end-side-tab-slot")["finishedProduct"]
        before = copy.deepcopy(product)
        plans = [self.assembly.preview_plan(template_id, finished_product=product)
                 for template_id in ids]
        self.assertEqual(product, before)
        baseline = plans[0]["designParts"]
        for plan in plans[1:]:
            for first, actual in zip(baseline, plan["designParts"]):
                self.assertEqual(first["role"], actual["role"])
                self.assertEqual(first["request"]["profileRef"], actual["request"]["profileRef"])
                self.assertEqual(first["request"]["parameters"], actual["request"]["parameters"])
                self.assertEqual(first["request"]["length"], actual["request"]["length"])
                for expected, value in zip(first["matrix"], actual["matrix"]):
                    self.assertAlmostEqual(expected, value)


if __name__ == "__main__":
    unittest.main()
