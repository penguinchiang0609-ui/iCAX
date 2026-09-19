"""Product frames consume only current mould-library groove definitions."""
import json
import unittest

from SingleSecurityWindowGeometryTests import PACKAGE, generate, graph


REMOVED_FIELDS = {
    "vGrooveBottomDistance", "vGrooveRadius", "vGrooveKFactor",
    "vGrooveMaleFemale", "vGrooveBottomCut", "vGrooveReliefHole",
    "vGrooveWallOvercut", "vGrooveReliefDiameter", "vGrooveReliefNoThrough",
}


class ProductWindowGrooveTests(unittest.TestCase):
    def test_descriptor_exposes_only_library_backed_join(self):
        descriptor = json.loads((PACKAGE / "template.json").read_text(encoding="utf-8"))
        fields = {item["key"]: item for item in descriptor["parameters"]}
        self.assertTrue(REMOVED_FIELDS.isdisjoint(fields))
        for key in ("frameJoinType", "doorFrameJoinType", "doorLeafFrameJoinType"):
            values = {choice["value"] for choice in fields[key]["choices"]}
            self.assertEqual({"v_groove_90:tool_library", "miter_45", "butt_90"}, values)

    def test_manufacturing_grooves_are_emitted_by_selected_tool_package(self):
        document = generate(frameLayout="four_sides", frameJoinType="v_groove_90:tool_library")
        keys = set(graph(document))
        self.assertTrue(any(".export.groove." in key and ".mould." in key for key in keys))
        self.assertFalse(any(key.endswith(".export.groove.0001.profile") for key in keys))

    def test_current_edge_arc_tool_can_be_selected_directly(self):
        document = generate(frameLayout="four_sides", frameJoinType="v_groove_90:tool_library",
                            outerFrameGrooveTool="system:edge-arc-groove")
        keys = set(graph(document))
        self.assertTrue(any(".export.groove." in key and ".mould." in key for key in keys))

    def test_removed_product_local_join_values_are_rejected(self):
        for style in ("sharp_v", "rounded_v", "left_arc", "right_arc"):
            with self.subTest(style=style), self.assertRaises(ValueError):
                generate(frameLayout="four_sides", frameJoinType=f"v_groove_90:{style}")


if __name__ == "__main__":
    unittest.main()
