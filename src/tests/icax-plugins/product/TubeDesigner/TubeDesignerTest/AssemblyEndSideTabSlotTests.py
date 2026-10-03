import copy
import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps/tube-designer/templates/_shared/assembly_template_runtime.py"


def load_runtime():
    spec = importlib.util.spec_from_file_location("assembly_template_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class EndSideTabSlotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load_runtime()

    def actual_participants(self):
        section = {
            "schema": "icax.tube-profile", "schemaVersion": 1,
            "id": "rect", "kind": "rect", "width": 60, "depth": 40,
            "parameters": {"width": 60, "depth": 40, "wallThickness": 2},
            "contours": [{"isHole": False, "points": [
                [-30, -20], [30, -20], [30, 20], [-30, 20]]}],
        }
        host_section = copy.deepcopy(section)
        host_section["sectionFrame"] = {"verification": "committed-source-brep-replay"}
        branch_section = copy.deepcopy(section)
        branch_section["sectionFrame"] = {
            "verification": "committed-source-brep-replay",
            "branchManufacturingZHostToolPart": [1, 0, 0],
            "xAxisToolPart": [0, 1, 0],
            "yAxisToolPart": [-1, 0, 0],
        }
        return [
            {"role": "host", "memberEntityId": "host-1", "itemKey": "host-1",
             "length": 400, "section": host_section,
             "anchor": {"kind": "side", "face": "top", "reference": "start",
                        "station": 200, "offset": 0, "rotation": 0}},
            {"role": "branch", "memberEntityId": "branch-1", "itemKey": "branch-1",
             "length": 180, "section": branch_section,
             "anchor": {"kind": "end", "end": "start", "rotation": 0, "trim": 0,
                        "stockAllowance": 30, "contactInset": 20}},
        ]

    def test_t_topology_and_mid_side_pose_are_independent_of_process_count(self):
        descriptor = self.runtime._template_by_id("end-side-tab-slot")
        self.assertEqual(descriptor["layoutShape"], "t")
        self.assertEqual(descriptor["productBinding"]["compatibleProductTopologies"],
                         [{"topology": "T"}])
        self.assertEqual(descriptor["productBinding"]["anchors"],
                         {"host": "side", "branch": "end"})
        plans = [self.runtime.preview_plan("end-side-tab-slot", {"pairCount": count})
                 for count in ("two", "four")]
        self.assertEqual([plan["parameters"]["pairCount"] for plan in plans],
                         ["two", "four"])
        for plan, count in zip(plans, (2, 4)):
            self.assertEqual(len(plan["designParts"]), 2)
            self.assertEqual(len(plan["manufacturingParts"]), 2)
            host, branch = [part["request"] for part in plan["manufacturingParts"]]
            self.assertEqual(host["length"], 440)
            self.assertEqual(branch["length"], 232)
            self.assertEqual(len(host["features"]), 1)
            slot = host["features"][0]
            tabs = branch["ends"]["end"]
            self.assertEqual(slot["toolRef"]["id"], "paired-side-slots")
            self.assertEqual(tabs["toolRef"]["id"], "paired-end-tabs")
            self.assertEqual(slot["toolParameters"]["pairCount"], count)
            self.assertEqual(tabs["toolParameters"]["pairCount"], count)
            self.assertFalse(slot["toolParameters"]["allowEndOpening"])
            self.assertEqual((slot["reference"], slot["station"], slot["face"]),
                             ("center", 0, "top"))
            self.assertEqual(slot["section"]["parameters"]["width"], 40)
            self.assertEqual(tabs["section"]["parameters"]["width"], 80)
            self.assertEqual(tabs["toolParameters"]["tabLength"],
                             slot["toolParameters"]["tabLength"])
            self.assertEqual(tabs["toolParameters"]["tabWidth"],
                             slot["toolParameters"]["tabWidth"])
            self.assertEqual(len(plan["resolvedWorkflow"]["partOperations"]), 2)
        self.assertEqual(plans[0]["designParts"], plans[1]["designParts"])
        branch_design = plans[0]["designParts"][1]
        # Product geometry: the branch approaches the host's +Z wall along
        # -Z. Its finished body stops at that face regardless of tab length.
        for actual, expected in zip(branch_design["matrix"][8:12], [-1.0, 0.0, 0.0, 240.0]):
            self.assertAlmostEqual(actual, expected, places=9)
        shorter_tabs = self.runtime.preview_plan("end-side-tab-slot", {"tabLength": 6})
        self.assertEqual(plans[0]["designParts"], shorter_tabs["designParts"])
        self.assertEqual(shorter_tabs["manufacturingParts"][1]["request"]["length"], 226)

    def test_t_product_binding_uses_real_side_and_end_anchors(self):
        participants = self.actual_participants()
        original = copy.deepcopy(participants)
        for count, expected in (("two", 2), ("four", 4)):
            with self.subTest(pairCount=count):
                plan = self.runtime.resolve_bound_plan("end-side-tab-slot",
                    {"pairCount": count}, participants=participants, product_topology="T")
                self.assertTrue(plan["supported"])
                self.assertEqual(len(plan["fitChecks"]), 1)
                fit = plan["fitChecks"][0]
                self.assertEqual(fit["kind"], "end-side-tab-slot")
                self.assertEqual(fit["hostFaceSource"], "anchor.face")
                self.assertEqual((fit["clearanceMm"], fit["insertionMm"]), (0.2, 12))
                host, branch = plan["parts"]
                self.assertEqual(host["role"], "host")
                self.assertEqual(branch["role"], "branch")
                self.assertEqual(len(host["features"]), 1)
                self.assertEqual(host["features"][0]["face"], "top")
                self.assertEqual(host["features"][0]["station"], 200)
                self.assertEqual(host["features"][0]["toolParameters"]["pairCount"], expected)
                self.assertEqual(branch["ends"]["start"]["toolParameters"]["pairCount"], expected)
                self.assertEqual(branch["ends"]["start"]["trim"], 38)
                self.assertEqual(plan["parameters"]["pairCount"], count)
        self.assertEqual(participants, original, "binding must not change source members")
        with self.assertRaisesRegex(ValueError, "成品拓扑不兼容"):
            self.runtime.resolve_bound_plan("end-side-tab-slot", participants=participants,
                                            product_topology="L")
        missing_contact = copy.deepcopy(participants)
        del missing_contact[1]["anchor"]["contactInset"]
        with self.assertRaisesRegex(ValueError, "缺少产品声明的接头近侧距离"):
            self.runtime.resolve_bound_plan("end-side-tab-slot", participants=missing_contact,
                                            product_topology="T")
        missing_frame = copy.deepcopy(participants)
        del missing_frame[1]["section"]["sectionFrame"]
        with self.assertRaisesRegex(ValueError, "已核对的真实制造截面"):
            self.runtime.resolve_bound_plan("end-side-tab-slot", participants=missing_frame,
                                            product_topology="T")

    def test_fit_rule_must_match_selected_real_processes(self):
        descriptor = copy.deepcopy(self.runtime._template_by_id("end-side-tab-slot"))
        descriptor["productBinding"]["fitChecks"][0]["processIds"] = ["host-slots-two"]
        with self.assertRaisesRegex(ValueError, "工序与实际参数分支不一致"):
            self.runtime._resolved_product_fit_checks(descriptor, {
                "pairCount": "two", "tabWidth": 8, "tabLength": 12, "sideClearance": 0.2,
            }, [process for process in descriptor["partProcesses"]
                if process["id"] in ("host-slots-two", "branch-tabs-two")], "T")


if __name__ == "__main__":
    unittest.main()
