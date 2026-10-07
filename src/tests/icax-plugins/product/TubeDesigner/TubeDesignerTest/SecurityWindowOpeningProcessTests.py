"""Processed opening frames: real graph, stock lengths, holes and identities."""
import unittest
from SecurityWindowRulesTests import build, template_input, NAMES, REVIEW


class SecurityWindowOpeningProcessTests(unittest.TestCase):
    def test_selected_library_tools_drive_each_continuous_opening_frame(self):
        for name in NAMES:
            for purpose in ("display", "manufacturing"):
                document = build(name, purpose,
                                 doorFrameJoinType="v_groove_90:tool_library",
                                 doorLeafFrameJoinType="v_groove_90:tool_library",
                                 doorFrameGrooveTool="system:v-notch-sharp",
                                 doorLeafFrameGrooveTool="system:edge-arc-groove")
                mould = [n["key"] for n in document["geometry"] if ".mould." in n["key"]]
                self.assertEqual(purpose == "manufacturing", bool(mould))

    def test_all_public_frame_processes_generate_for_each_layout(self):
        _, defaults, _ = template_input(NAMES[0])
        processes = ("butt_90", "miter_45", "v_groove_90:tool_library")
        for name in NAMES:
            for process in processes:
                for purpose in ("display", "manufacturing"):
                    with self.subTest(template=name, process=process, purpose=purpose):
                        document = build(name, purpose, doorFrameJoinType=process, doorLeafFrameJoinType=process)
                        items = {item["key"]: item for item in document["items"]}
                        geometry = {node["key"]: node for node in document["geometry"]}
                        self.assertEqual(len(geometry), len(document["geometry"]))
                        for node in geometry.values():
                            self.assertTrue(all(key in geometry for key in node.get("inputs", [])))
                        for relationship in document["relationships"]:
                            self.assertTrue(all(key in items for key in relationship["items"]))
                        for prefix in ("access_door.fixed_frame.", "access_door.leaf.frame."):
                            frames = [item for key, item in items.items() if key.startswith(prefix)]
                            continuous_stock = purpose == "manufacturing" and process.startswith("v_groove")
                            self.assertEqual(1 if continuous_stock else 4, len(frames))
                            if continuous_stock:
                                self.assertGreater(frames[0]["properties"]["length"], 2000)
                                self.assertEqual(4, len(frames[0]["properties"]["tubeDesigner.cornerProcess"]["bendLocations"]))
                        grooves = [key for key in geometry if ".export.groove." in key]
                        self.assertEqual(purpose == "manufacturing" and process.startswith("v_groove"), bool(grooves))
                        if purpose == "manufacturing" and process.startswith("v_groove"):
                            apertures=[record for record in document['extensions']['tubeDesigner.assemblyGeometryProcesses']['instances']
                                       if record['templateId']=='tube-profile-aperture'
                                       and record['stockId'].startswith('access_door.leaf.frame.continuous')]
                            self.assertTrue(apertures)
                            self.assertTrue(all(record['result']['operations'] for record in apertures))
                        self.assertEqual(purpose == "manufacturing" and process.startswith("v_groove"),
                                         document["extensions"][REVIEW].get("vGrooveTrialRequired", False))

    def test_mixed_frame_processes_on_side_and_cap_planes(self):
        for name, field, face, face_name in (
                (NAMES[1], "accessDoorFace2", "side", "右侧面"),
                (NAMES[2], "accessDoorFace3", "left", "左侧面"),
                (NAMES[2], "accessDoorFace3", "right", "右侧面"),
                (NAMES[3], "accessDoorFace5", "left", "左侧面"),
                (NAMES[3], "accessDoorFace5", "right", "右侧面"),
                (NAMES[3], "accessDoorFace5", "bottom", "下面")):
            with self.subTest(template=name, face=face):
                document = build(name, "manufacturing", **{field: face},
                                 doorClearWidth=250, doorClearHeight=250, doorUOffset=100, doorVOffset=100,
                                 doorHorizontalTopCenterOffset=99,
                                 doorHorizontalBottomCenterOffset=99,
                                 doorFrameJoinType="miter_45",
                                 doorLeafFrameJoinType="v_groove_90:tool_library")
                frames = [item for item in document["items"] if item["key"].startswith("access_door.leaf.frame.")]
                self.assertEqual(1, len(frames))
                self.assertEqual({face_name},
                                 {item["properties"]["tubeDesigner.faceName"] for item in frames})
                hinges = [r for r in document["relationships"] if r["kind"] == "hinge"]
                self.assertEqual(2, len(hinges))
                self.assertTrue(all(frames[0]["key"] in r["items"] for r in hinges))

    def test_wrap_direction_changes_cut_lengths_but_not_clear_opening(self):
        for name in NAMES:
            a = build(name, "manufacturing", doorFrameJoinType="butt_90", doorFrameButtWrapMode="side_wraps_horizontal")
            b = build(name, "manufacturing", doorFrameJoinType="butt_90", doorFrameButtWrapMode="horizontal_wraps_side")
            def length(document, side):
                return next(i["properties"]["length"] for i in document["items"] if i["key"] == f"access_door.fixed_frame.{side}.0001")
            self.assertAlmostEqual(50, length(a, "left") - length(b, "left"))
            self.assertAlmostEqual(50, length(b, "top") - length(a, "top"))
            self.assertEqual(a["extensions"][REVIEW]["fixedClearWidth"], b["extensions"][REVIEW]["fixedClearWidth"])

    def test_disabled_and_non_bending_openings_ignore_hidden_process_values(self):
        for name in NAMES:
            disabled = build(name, accessDoorEnabled=False,
                             doorFrameJoinType="v_groove_90:tool_library",
                             doorLeafFrameJoinType="v_groove_90:tool_library")
            self.assertFalse(any(item["key"].startswith("access_door.")
                                 for item in disabled["items"]))
            self.assertFalse(disabled["extensions"][REVIEW].get("vGrooveTrialRequired", False))
            build(name, doorFrameJoinType="miter_45", doorLeafFrameJoinType="miter_45")
            with self.assertRaisesRegex(ValueError, "doorFrameJoinType 的 V 槽样式不支持：sharp_v"):
                build(name, "manufacturing", doorFrameJoinType="v_groove_90:sharp_v")


if __name__ == "__main__":
    unittest.main()
