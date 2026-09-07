"""Processed opening frames: real graph, stock lengths, holes and identities."""
import unittest
from SecurityWindowRulesTests import build, template_input, NAMES, REVIEW


class SecurityWindowOpeningProcessTests(unittest.TestCase):
    def test_all_public_frame_processes_generate_for_each_layout(self):
        _, defaults, _ = template_input(NAMES[0])
        processes = ("butt_90", "miter_45", "v_groove_90:sharp_v", "v_groove_90:rounded_v",
                     "v_groove_90:left_arc", "v_groove_90:right_arc")
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
                            self.assertEqual(1 if process.startswith("v_groove") else 4, len(frames))
                            if process.startswith("v_groove"):
                                self.assertGreater(frames[0]["properties"]["length"], 2000)
                                self.assertEqual(4, len(frames[0]["properties"]["tubeDesigner.cornerProcess"]["bendLocations"]))
                        grooves = [key for key in geometry if ".export.groove." in key]
                        self.assertEqual(purpose == "manufacturing" and process.startswith("v_groove"), bool(grooves))
                        if purpose == "manufacturing" and process.startswith("v_groove"):
                            self.assertTrue(any("access_door.leaf.frame.continuous" in key and ".export.through." in key for key in geometry))
                        self.assertEqual(process.startswith("v_groove"), document["extensions"][REVIEW]["vGrooveTrialRequired"])

    def test_mixed_frame_processes_on_side_and_cap_planes(self):
        for name, face in ((NAMES[1], "side"), (NAMES[2], "left"), (NAMES[2], "right"),
                           (NAMES[3], "left"), (NAMES[3], "right"), (NAMES[3], "top"), (NAMES[3], "bottom")):
            with self.subTest(template=name, face=face):
                document = build(name, "manufacturing", accessDoorFace=face, doorUse="maintenance",
                                 doorClearWidth=250, doorClearHeight=250, doorUOffset=100, doorVOffset=100,
                                 doorFrameJoinType="miter_45", doorLeafFrameJoinType="v_groove_90:sharp_v")
                frames = [item for item in document["items"] if item["key"].startswith("access_door.leaf.frame.")]
                self.assertEqual(1, len(frames))
                self.assertTrue(all(item["properties"]["tubeDesigner.faceName"] for item in frames))
                hinges = [r for r in document["relationships"] if r["kind"] == "hinge"]
                self.assertTrue(all(frames[0]["key"] in r["items"] for r in hinges))

    def test_wrap_direction_changes_cut_lengths_but_not_clear_opening(self):
        for name in NAMES:
            a = build(name, doorFrameJoinType="butt_90", doorFrameButtWrapMode="side_wraps_horizontal")
            b = build(name, doorFrameJoinType="butt_90", doorFrameButtWrapMode="horizontal_wraps_side")
            def length(document, side):
                return next(i["properties"]["length"] for i in document["items"] if i["key"] == f"access_door.fixed_frame.{side}.0001")
            self.assertAlmostEqual(50, length(a, "left") - length(b, "left"))
            self.assertAlmostEqual(50, length(b, "top") - length(a, "top"))
            self.assertEqual(a["extensions"][REVIEW]["fixedClearWidth"], b["extensions"][REVIEW]["fixedClearWidth"])

    def test_disabled_and_non_bending_openings_ignore_hidden_process_values(self):
        for name in NAMES:
            build(name, accessDoorEnabled=False, doorFrameJoinType="unused", doorLeafFrameJoinType="unused", vGrooveKFactor=float("nan"))
            build(name, doorFrameJoinType="miter_45", doorLeafFrameJoinType="miter_45", vGrooveKFactor=float("nan"))
            with self.assertRaisesRegex(ValueError, "K 因子"):
                build(name, doorFrameJoinType="v_groove_90:sharp_v", vGrooveKFactor=2)


if __name__ == "__main__":
    unittest.main()
