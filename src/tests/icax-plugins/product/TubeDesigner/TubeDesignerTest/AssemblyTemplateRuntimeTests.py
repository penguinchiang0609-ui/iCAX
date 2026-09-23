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

    def test_catalogue_is_grouped_by_part_count_and_connection_position(self):
        ids = {item["id"] for item in self.catalogue["assemblies"]}
        self.assertTrue({
            "two-end-end-angle", "two-end-middle", "mechanical-fastener",
            "three-end-end-end", "three-end-end-middle", "four-end-end-end-end",
            "insert-sleeve", "weld-interface", "bend", "tab-slot-lock",
        }.issubset(ids))
        self.assertTrue({"through-bolt", "slot-bolt-adjustable", "saddle-weld"}.isdisjoint(ids))
        categories = {item["category"] for item in self.catalogue["assemblies"]}
        self.assertEqual(categories, {
            "two-end-end", "two-end-middle", "two-middle-middle",
            "three-end-end-end", "three-end-end-middle", "four-end-end-end-end",
        })

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

    @staticmethod
    def _world_point(matrix, point):
        return [
            matrix[0] * point[0] + matrix[1] * point[1] + matrix[2] * point[2] + matrix[3],
            matrix[4] * point[0] + matrix[5] * point[1] + matrix[6] * point[2] + matrix[7],
            matrix[8] * point[0] + matrix[9] * point[1] + matrix[10] * point[2] + matrix[11],
        ]

    def assertWorldPointsAlmostEqual(self, left, right, places=7):
        for actual, expected in zip(left, right):
            self.assertAlmostEqual(actual, expected, places=places)

    @staticmethod
    def _translation(matrix):
        return [matrix[3], matrix[7], matrix[11]]

    @staticmethod
    def _axis(matrix):
        return [matrix[0], matrix[4], matrix[8]]

    def assertTranslationDelta(self, exploded, finished, expected, places=7):
        actual = [
            self._translation(exploded)[index] - self._translation(finished)[index]
            for index in range(3)
        ]
        self.assertWorldPointsAlmostEqual(actual, expected, places=places)

    def test_finished_scene_places_real_interfaces_together(self):
        angled = self.runtime.preview_plan("two-end-end-angle", {
            "jointAngle": 90, "fitGap": 0, "planeRotation": 0,
        })
        a, b = angled["designParts"]
        self.assertWorldPointsAlmostEqual(
            self._world_point(a["matrix"], [a["request"]["length"], 0, 0]),
            self._world_point(b["matrix"], [0, 0, 0]),
        )
        self.assertEqual(angled["manufacturingParts"][0]["request"]["ends"]["end"]["rotation"], 90)
        self.assertEqual(angled["manufacturingParts"][1]["request"]["ends"]["start"]["rotation"], 90)

        tab_slot = self.runtime.preview_plan("tab-slot-lock")
        tab, slot = tab_slot["designParts"]
        insertion_depth = 20 + 20 / 2
        self.assertWorldPointsAlmostEqual(
            self._world_point(tab["matrix"], [tab["request"]["length"] - insertion_depth, 0, 0]),
            self._world_point(slot["matrix"], [0, 0, 0]),
        )

        end_middle = self.runtime.preview_plan("two-end-middle", {
            "interfaceMode": "saddle", "intersectionAngle": 60, "fitGap": 0,
        })
        host, branch = end_middle["designParts"]
        self.assertWorldPointsAlmostEqual(
            self._world_point(host["matrix"], [host["request"]["length"] / 2, 0, 0]),
            self._world_point(branch["matrix"], [branch["request"]["length"], 0, 0]),
        )
        self.assertAlmostEqual(branch["matrix"][0], 0.5)
        self.assertAlmostEqual(branch["matrix"][4], -(3 ** 0.5) / 2)

    def test_exploded_scene_follows_each_assembly_path(self):
        tab_slot = self.runtime.preview_plan("tab-slot-lock")
        tab, slot = tab_slot["designParts"]
        tab_blank, slot_blank = tab_slot["manufacturingParts"]
        self.assertEqual(tab_blank["matrix"], tab_blank["explodedMatrix"])
        self.assertEqual(self._axis(tab_blank["matrix"]), self._axis(tab["matrix"]))
        self.assertEqual(self._axis(slot_blank["matrix"]), self._axis(slot["matrix"]))
        self.assertTranslationDelta(tab_blank["matrix"], tab["matrix"], [-100, 0, 0])
        self.assertTranslationDelta(slot_blank["matrix"], slot["matrix"], [100, 0, 0])

        angled = self.runtime.preview_plan("two-end-end-angle", {
            "jointAngle": 60, "fitGap": 0, "planeRotation": 35,
        })
        for finished, exploded in zip(angled["designParts"], angled["manufacturingParts"]):
            self.assertWorldPointsAlmostEqual(self._axis(exploded["matrix"]), self._axis(finished["matrix"]))
        self.assertTranslationDelta(angled["manufacturingParts"][0]["matrix"], angled["designParts"][0]["matrix"], [-100, 0, 0])

        end_middle = self.runtime.preview_plan("two-end-middle", {
            "interfaceMode": "saddle", "intersectionAngle": 60, "fitGap": 0,
        })
        self.assertTranslationDelta(end_middle["manufacturingParts"][0]["matrix"], end_middle["designParts"][0]["matrix"], [0, 0, 0])
        branch_axis = self._axis(end_middle["designParts"][1]["matrix"])
        self.assertTranslationDelta(
            end_middle["manufacturingParts"][1]["matrix"],
            end_middle["designParts"][1]["matrix"],
            [-100 * component for component in branch_axis],
        )

        bolted = self.runtime.preview_plan("through-bolt")
        self.assertTranslationDelta(bolted["manufacturingParts"][0]["matrix"], bolted["designParts"][0]["matrix"], [0, 0, -80])
        self.assertTranslationDelta(bolted["manufacturingParts"][1]["matrix"], bolted["designParts"][1]["matrix"], [0, 0, 80])

    def test_every_template_builds_a_two_to_four_part_interactive_preview_plan(self):
        for assembly in self.catalogue["assemblies"]:
            defaults = {item["key"]: item.get("defaultValue") for item in assembly["parameters"]}
            plan = self.runtime.preview_plan(assembly["id"], defaults)
            self.assertEqual(plan["schema"], "icax.assembly-preview-plan")
            self.assertEqual(plan["templateId"], assembly["id"])
            self.assertEqual(len(plan["designParts"]), len(assembly["participants"]))
            self.assertGreaterEqual(len(plan["designParts"]), 2)
            self.assertLessEqual(len(plan["designParts"]), 4)
            self.assertEqual(len(plan["manufacturingParts"]), assembly["outputs"]["manufacturingPartCount"])
            self.assertEqual(plan["parameters"], defaults)
            self.assertTrue(all(part["sourceRole"] for part in plan["manufacturingParts"]))
            expected_roles = {item["id"]: item["participants"] for item in assembly["manufacturingPlan"]["blankParts"]}
            for part in plan["manufacturingParts"]:
                self.assertEqual(part["participantRoles"], expected_roles[part["blankId"]])
                self.assertEqual(len(part["explodedMatrix"]), 16)
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

        bolted = self.runtime.preview_plan("mechanical-fastener", {"nominalDiameter": 12, "holeClearance": 1, "count": 3, "pitch": 45})
        self.assertEqual(len(bolted["manufacturingParts"]), 2)
        for part in bolted["manufacturingParts"]:
            hole = part["request"]["features"][0]
            self.assertEqual(hole["toolParameters"]["diameter"], 13)
            self.assertEqual(hole["arrayCount"], 3)
            self.assertEqual(hole["arrayPitch"], 45)
            self.assertTrue(hole["opposite"])

        saddle = self.runtime.preview_plan("two-end-middle", {"interfaceMode": "saddle"})
        branch_end = saddle["manufacturingParts"][1]["request"]["ends"]["end"]
        self.assertEqual(branch_end["toolRef"]["id"], "end-profile")
        self.assertEqual(branch_end["section"]["profileRef"]["id"], "rect")

    def test_end_middle_modes_cut_from_the_branch_profile_and_sleeve_stays_end_end(self):
        plans = {mode: self.runtime.preview_plan("two-end-middle", {"interfaceMode": mode})
                 for mode in ("singleInsert", "throughInsert", "saddle")}
        end_feature = plans["singleInsert"]["manufacturingParts"][0]["request"]["features"][0]
        through_feature = plans["throughInsert"]["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(end_feature["toolRef"]["id"], "branch-profile")
        self.assertEqual(end_feature["direction"], "negative")
        self.assertEqual(through_feature["direction"], "through")
        self.assertEqual(end_feature["section"]["profileRef"]["id"], "round")
        self.assertEqual(plans["saddle"]["manufacturingParts"][1]["request"]["ends"]["end"]["toolRef"]["id"], "end-profile")
        for mode in ("sleeve", "telescopic"):
            sleeve = self.runtime.preview_plan("insert-sleeve", {"connectionMode": mode})
            self.assertEqual(sleeve["manufacturingParts"][1]["request"]["features"], [])

    def test_fastener_family_switches_only_the_required_hole_process(self):
        for kind in ("bolt", "pin", "rivet", "rivetNut", "selfTapping", "tapped"):
            plan = self.runtime.preview_plan("mechanical-fastener", {"fastenerType": kind})
            self.assertEqual(plan["manufacturingParts"][0]["request"]["features"][0]["toolRef"]["id"], "circle")
            self.assertEqual(plan["manufacturingParts"][1]["request"]["features"][0]["toolRef"]["id"], "circle")
        adjustable = self.runtime.preview_plan("mechanical-fastener", {"fastenerType": "adjustableBolt", "adjustment": 18})
        slot = adjustable["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(slot["toolRef"]["id"], "slot")
        self.assertEqual(slot["toolParameters"]["spanAlong"], 29)

    def test_weld_family_generates_the_interface_specific_blank(self):
        lap = self.runtime.preview_plan("weld-interface", {"weldType": "lap"})
        plug = self.runtime.preview_plan("weld-interface", {"weldType": "plug"})
        slot = self.runtime.preview_plan("weld-interface", {"weldType": "slot"})
        self.assertEqual(lap["manufacturingParts"][1]["request"]["features"], [])
        self.assertEqual(plug["manufacturingParts"][1]["request"]["features"][0]["toolRef"]["id"], "circle")
        self.assertEqual(slot["manufacturingParts"][1]["request"]["features"][0]["toolRef"]["id"], "slot")

    def test_three_and_four_part_scenes_return_real_blanks_for_every_role(self):
        for template_id, expected in (("three-end-end-end", 3), ("three-end-end-middle", 3), ("four-end-end-end-end", 4)):
            plan = self.runtime.preview_plan(template_id)
            self.assertEqual(len(plan["designParts"]), expected)
            self.assertEqual(len(plan["manufacturingParts"]), expected)
            self.assertEqual(
                {part["role"] for part in plan["designParts"]},
                {part["sourceRole"] for part in plan["manufacturingParts"]},
            )
            self.assertTrue(any(
                part["request"]["ends"]["start"].get("type") != "keep"
                or part["request"]["ends"]["end"].get("type") != "keep"
                for part in plan["manufacturingParts"]
            ))

    def test_spatial_bend_rotates_the_bend_plane_without_changing_the_planar_default(self):
        planar = self.runtime.preview_plan("bend", {"bendPlane": "planar", "angle": 75})
        spatial = self.runtime.preview_plan("bend", {"bendPlane": "spatial", "planeRotation": 45, "angle": 75})
        self.assertNotEqual(planar["designParts"][1]["matrix"], spatial["designParts"][1]["matrix"])
        self.assertAlmostEqual(planar["designParts"][1]["matrix"][1], 0.0)
        self.assertNotAlmostEqual(spatial["designParts"][1]["matrix"][1], 0.0)


if __name__ == "__main__":
    unittest.main()
