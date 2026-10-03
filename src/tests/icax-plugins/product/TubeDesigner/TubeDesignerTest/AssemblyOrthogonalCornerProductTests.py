"""Replay the three raw members used by the native corner binding tests."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import unittest

SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template
import AssemblyFrameLTTests as frame_replay

DIRECTORY = SRC / "apps/tube-designer/templates/product/assembly_orthogonal_corner"
DESCRIPTOR = json.loads((DIRECTORY / "template.json").read_text(encoding="utf-8"))
DEFAULTS = {field["key"]: field["defaultValue"] for field in DESCRIPTOR["parameters"]}
MODULE = _load_template(str(DIRECTORY / "template.py"), "orthogonal-product-tests")


class AssemblyOrthogonalCornerProductTests(unittest.TestCase):
    def build(self, changes=None, purpose="manufacturing"):
        parameters = {**DEFAULTS, **(changes or {})}
        before = deepcopy(parameters)
        result = MODULE.generate(parameters, {"template": DESCRIPTOR,
                                               "geometryPurpose": purpose})
        self.assertEqual(parameters, before)
        self.assertEqual(result["parameters"], before)
        self.assertEqual(result["template"]["id"], DESCRIPTOR["id"])
        return result

    def test_raw_members_share_one_end_node_without_product_chosen_tooling(self):
        result = self.build()
        self.assertEqual([item["key"] for item in result["items"]],
                         ["corner.a.0001", "corner.b.0001", "corner.c.0001"])
        self.assertEqual(len(result["tables"][0]["rows"]), 3)
        self.assertEqual(len(result["relationships"]), 1)
        node = result["relationships"][0]["properties"]
        self.assertEqual(node["topology"], "orthogonal-corner")
        self.assertEqual([anchor["end"] for anchor in node["participantAnchors"]],
                         ["end", "start", "start"])
        for anchor in node["participantAnchors"]:
            self.assertEqual(anchor["centerlinePoint"], [0, 0, 0])
        axes = [item["properties"]["tubeDesigner.manufacturingAxis"]
                for item in result["items"]]
        for i, first in enumerate(axes):
            for second in axes[i + 1:]:
                self.assertAlmostEqual(sum(a * b for a, b in zip(first, second)), 0)
        self.assertFalse(any(node["operator"] == "boolean" for node in result["geometry"]))
        for item in result["items"]:
            self.assertEqual(item["properties"]["assemblyFrame.member"]["stockState"], "uncut")
            self.assertFalse(item["properties"]["tubeDesigner.assemblyPlanning"]["ready"])

    def test_section_frames_replay_actual_source_for_both_geometry_purposes(self):
        for purpose in ("display", "manufacturing"):
            with self.subTest(purpose=purpose):
                result = self.build(purpose=purpose)
                frame_replay.AssemblyFrameLTTests.assert_section_frames_replay_source(self, result)

    def test_blank_allowances_follow_opposing_section_not_example_dimensions(self):
        result = self.build({"aDepth": 64, "bDepth": 48,
                             "aLength": 330, "bLength": 410, "cLength": 190})
        frame_replay.AssemblyFrameLTTests.assert_section_frames_replay_source(self, result)
        members = [item["properties"]["assemblyFrame.member"] for item in result["items"]]
        self.assertEqual(members[0]["stockInterval"],
                         {"startStation": 0.0, "endStation": 354.0})
        self.assertEqual(members[1]["stockInterval"],
                         {"startStation": -32.0, "endStation": 410.0})
        self.assertEqual(members[2]["stockInterval"],
                         {"startStation": 0.0, "endStation": 190.0})
        anchors = result["relationships"][0]["properties"]["participantAnchors"]
        for anchor, expected in zip(anchors, [24.0, 32.0, 0.0]):
            self.assertAlmostEqual(anchor["anchor"]["stockAllowance"], expected)

    def test_invalid_length_cannot_produce_a_false_three_edge_node(self):
        for value in (True, 0, 99, 2001, float("nan"), float("inf")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.build({"cLength": value})


if __name__ == "__main__":
    unittest.main()
