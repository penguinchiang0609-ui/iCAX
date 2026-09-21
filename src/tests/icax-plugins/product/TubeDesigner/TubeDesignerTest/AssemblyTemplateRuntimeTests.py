import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps" / "tube-designer" / "templates" / "_shared" / "assembly_template_runtime.py"


def load_runtime():
    spec = importlib.util.spec_from_file_location("assembly_template_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AssemblyTemplateRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = load_runtime()
        cls.catalogue = cls.runtime.catalogue()

    def test_all_descriptors_are_valid(self):
        self.assertEqual(self.catalogue["errors"], [])
        self.assertGreaterEqual(len(self.catalogue["assemblies"]), 5)

    def test_bend_supports_cold_and_notched_realization(self):
        bend = next(item for item in self.catalogue["assemblies"] if item["id"] == "bend")
        self.assertEqual(bend["manufacturingPlan"]["realization"], "integrated")
        self.assertEqual(len(bend["participants"]), 2)
        self.assertEqual(len(bend["manufacturingPlan"]["blankParts"]), 1)
        method = next(item for item in bend["parameters"] if item["key"] == "bendMethod")
        self.assertEqual([item["value"] for item in method["options"]], ["cold", "notched"])
        selection = bend["partProcesses"][0]["resourceSelection"]
        self.assertEqual(selection["parameter"], "slotProcess")
        self.assertEqual(selection["category"], "槽口")
        self.assertEqual(
            [item["id"] for item in selection["resources"]],
            [
                "segmented-bend", "v-notch-sharp", "embedded-arc-notch",
                "edge-arc-groove", "flexible-slit-bend",
            ],
        )
        self.assertTrue(all(item["parameters"] for item in selection["resources"]))
        bindings = bend["partProcesses"][0]["parameterBindingsByResource"]
        self.assertEqual(bindings["embedded-arc-notch"], {"angle": "$angle"})
        self.assertEqual(bindings["edge-arc-groove"], {"angle": "$angle"})
        self.assertEqual(bindings["flexible-slit-bend"], {"angle": "$angle", "bendRadius": "$bendRadius"})
        embedded = next(item for item in selection["resources"] if item["id"] == "embedded-arc-notch")
        embedded_keys = {item["key"] for item in embedded["parameters"]}
        self.assertIn("arcRadius", embedded_keys)
        self.assertNotIn("bendRadius", embedded_keys)

    def test_assembly_reuses_single_part_processes(self):
        for assembly in self.catalogue["assemblies"]:
            for process in assembly["partProcesses"]:
                self.assertTrue(process.get("resource") or process.get("resourceSelection"))
                if process.get("resource"):
                    self.assertEqual(process["resource"]["kind"], "part-process")
                    self.assertIn("descriptor", process["resource"])

    def test_tab_slot_binds_the_actual_joint_contract(self):
        assembly = next(item for item in self.catalogue["assemblies"] if item["id"] == "tab-slot-lock")
        keys = {item["key"] for item in assembly["parameters"]}
        self.assertIn("straightDepth", keys)
        self.assertNotIn("tipRadius", keys)
        processes = {item["id"]: item["parameterBindings"] for item in assembly["partProcesses"]}
        self.assertEqual("male", processes["tab-end"]["gender"])
        self.assertEqual("female", processes["slot-end"]["gender"])
        self.assertEqual("$straightDepth", processes["tab-end"]["straightDepth"])
        self.assertEqual("$straightDepth", processes["slot-end"]["straightDepth"])
        self.assertNotIn("cornerRadius", processes["tab-end"])
        self.assertNotIn("cornerRadius", processes["slot-end"])
        self.assertEqual("$fitClearance", processes["slot-end"]["sideClearance"])
        self.assertEqual("$fitClearance", processes["slot-end"]["axialClearance"])
        self.assertNotIn("clearance", processes["slot-end"])

    def test_every_template_builds_a_two_part_interactive_preview_plan(self):
        for assembly in self.catalogue["assemblies"]:
            defaults = {item["key"]: item.get("defaultValue") for item in assembly["parameters"]}
            plan = self.runtime.preview_plan(assembly["id"], defaults)
            self.assertEqual(plan["schema"], "icax.assembly-preview-plan")
            self.assertEqual(plan["templateId"], assembly["id"])
            self.assertEqual(len(plan["designParts"]), 2)
            self.assertEqual(len(plan["manufacturingParts"]), assembly["outputs"]["manufacturingPartCount"])
            self.assertEqual(plan["parameters"], defaults)
            self.assertTrue(all(part["sourceRole"] for part in plan["manufacturingParts"]))
            for part in plan["designParts"] + plan["manufacturingParts"]:
                self.assertEqual(len(part["matrix"]), 16)
                self.assertEqual(len(part["compareMatrix"]), 16)
                self.assertGreater(part["request"]["length"], 0)
            if len(plan["manufacturingParts"]) > 1:
                blank_poses = {tuple(part["matrix"]) for part in plan["manufacturingParts"]}
                self.assertEqual(len(blank_poses), len(plan["manufacturingParts"]),
                                 f"{assembly['id']} 的下料件必须在右侧视口分开展示")

    def test_preview_plan_changes_from_logical_placement_to_actual_blank_processes(self):
        cold = self.runtime.preview_plan("bend", {"bendMethod": "cold", "angle": 60, "bendRadius": 40, "bendFactor": 0.5, "slotProcess": "segmented-bend"})
        notched = self.runtime.preview_plan("bend", {"bendMethod": "notched", "angle": 60, "bendRadius": 40, "bendFactor": 0.5, "slotProcess": "v-notch-sharp"})
        self.assertNotEqual(cold["designParts"][1]["matrix"], self.runtime.preview_plan("bend", {"bendMethod": "cold", "angle": 90, "bendRadius": 40, "bendFactor": 0.5, "slotProcess": "segmented-bend"})["designParts"][1]["matrix"])
        self.assertEqual(cold["manufacturingParts"][0]["request"]["features"], [])
        self.assertGreater(cold["manufacturingParts"][0]["request"]["length"], notched["manufacturingParts"][0]["request"]["length"])
        feature = notched["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(feature["toolRef"]["id"], "v-notch-sharp")
        self.assertEqual(feature["toolParameters"]["angle"], 60)

        embedded = self.runtime.preview_plan("bend", {
            "bendMethod": "notched", "angle": 90, "bendRadius": 40,
            "bendFactor": 0.5, "slotProcess": "embedded-arc-notch",
        })
        embedded_tool = embedded["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]
        self.assertEqual(embedded_tool["angle"], 90)
        self.assertEqual(embedded_tool["arcRadius"], 10)
        self.assertNotIn("bendRadius", embedded_tool)
        customized = self.runtime.preview_plan("bend", {
            "bendMethod": "notched", "angle": 90, "bendRadius": 55,
            "bendFactor": 0.5, "slotProcess": "embedded-arc-notch",
        }, {"bend-slot": {"embedded-arc-notch": {"arcRadius": 12}}})
        customized_tool = customized["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]
        self.assertEqual(customized_tool["arcRadius"], 12)
        self.assertEqual(customized["parameters"]["bendRadius"], 55)

        edge_arc = self.runtime.preview_plan("bend", {
            "bendMethod": "notched", "angle": 90, "bendRadius": 40,
            "bendFactor": 0.5, "slotProcess": "edge-arc-groove",
        })
        edge_arc_tool = edge_arc["manufacturingParts"][0]["request"]["features"][0]["toolParameters"]
        self.assertEqual(edge_arc_tool["angle"], 90)
        self.assertEqual(edge_arc_tool["bridge"], 1)
        self.assertTrue(edge_arc_tool["leftArc"])
        self.assertNotIn("bendRadius", edge_arc_tool)

        for slot_process, radius_key in (("flexible-slit-bend", "bendRadius"),):
            planned = self.runtime.preview_plan("bend", {
                "bendMethod": "notched", "angle": 75, "bendRadius": 48,
                "bendFactor": 0.5, "slotProcess": slot_process,
            })
            tool = planned["manufacturingParts"][0]["request"]["features"][0]
            self.assertEqual(tool["toolRef"]["id"], slot_process)
            self.assertEqual(tool["toolParameters"]["angle"], 75)
            self.assertEqual(tool["toolParameters"][radius_key], 48)

        circle_wrap = self.runtime.preview_plan("bend", {
            "bendMethod": "notched", "angle": 75, "bendRadius": 48,
            "bendFactor": 0.5, "slotProcess": "v-notch-sharp",
        }, {"bend-slot": {"v-notch-sharp": {
            "bottomStrategy": "relief", "reliefShape": "circleWrap",
            "enclosedDiameter": 16, "radialClearance": 0.1,
        }}})
        circle_tool = circle_wrap["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(circle_tool["toolRef"]["id"], "v-notch-sharp")
        self.assertEqual(circle_tool["toolParameters"]["angle"], 75)
        self.assertEqual(circle_tool["toolParameters"]["bottomStrategy"], "relief")
        self.assertEqual(circle_tool["toolParameters"]["reliefShape"], "circleWrap")
        self.assertNotIn("bendRadius", circle_tool["toolParameters"])

        bolted = self.runtime.preview_plan("through-bolt", {"boltDiameter": 12, "holeClearance": 1, "boltCount": 3, "pitch": 45, "washer": "both"})
        self.assertEqual(len(bolted["manufacturingParts"]), 2)
        for part in bolted["manufacturingParts"]:
            hole = part["request"]["features"][0]
            self.assertEqual(hole["toolParameters"]["diameter"], 13)
            self.assertEqual(hole["arrayCount"], 3)
            self.assertEqual(hole["arrayPitch"], 45)
            self.assertTrue(hole["opposite"])

        saddle = self.runtime.preview_plan("saddle-weld")
        branch_end = saddle["manufacturingParts"][0]["request"]["ends"]["start"]
        self.assertEqual(branch_end["toolRef"]["id"], "end-profile")
        self.assertEqual(branch_end["section"]["profileRef"]["id"], "round")


if __name__ == "__main__":
    unittest.main()
