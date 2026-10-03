"""The segmented assembly preview must follow its cutting-tool slot layout."""

import importlib.util
import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps" / "tube-designer" / "templates" / "_shared" / "assembly_template_runtime.py"


def runtime():
    spec = importlib.util.spec_from_file_location("segmented_assembly_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SegmentedBendAssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runtime()

    def preview(self, values=None, tool_values=None):
        drafts = {"node-slot": {"segmented-bend": tool_values or {}}}
        return self.runtime.preview_plan("segmented-bend", values or {}, drafts)

    def test_default_preview_has_tool_matched_six_hinge_target_and_one_processed_blank(self):
        plan = self.preview()
        mesh = plan["formedPreviewMesh"]
        metadata = mesh["metadata"]
        pitch = 2 * (40 + 20) * math.sin(math.radians(90 / 6) / 2)
        self.assertEqual(metadata["segmentCount"], 6)
        self.assertEqual(metadata["formingValidation"], "not-performed")
        self.assertEqual(metadata["previewKind"], "target-shape")
        self.assertAlmostEqual(metadata["slotPitch"], pitch)
        self.assertAlmostEqual(metadata["patternLength"], 5 * pitch + metadata["singleNotchOpening"])
        self.assertAlmostEqual(plan["manufacturingParts"][0]["request"]["length"],
                               520 + metadata["patternLength"])
        self.assertEqual(len(plan["manufacturingParts"]), 1)
        self.assertEqual(plan["manufacturingParts"][0]["request"]["features"][0]["toolRef"]["id"], "segmented-bend")
        self.assertEqual(metadata["stationCount"], 8, "six distinct hinges plus two straight ends")
        self.assertEqual(plan["parameters"], {"angle": 90, "bendRadius": 40})
        anchors = metadata["annotations"]["anchors"]
        self.assertEqual(len(anchors["hingeCenters"]), 6)
        self.assertAlmostEqual(math.dist(anchors["straightStart"], anchors["firstUncutEdge"]), 260)
        self.assertAlmostEqual(math.dist(anchors["firstUncutEdge"], anchors["firstHinge"]),
                               metadata["singleNotchOpening"] / 2)
        self.assertAlmostEqual(math.dist(anchors["firstHinge"], anchors["secondHinge"]), pitch)
        self.assertAlmostEqual(math.dist(anchors["lastHinge"], anchors["lastUncutEdge"]),
                               metadata["singleNotchOpening"] / 2)
        self.assertAlmostEqual(math.dist(anchors["lastUncutEdge"], anchors["straightEnd"]), 260)
        for index, (start, end) in enumerate(zip(anchors["hingeCenters"], anchors["hingeCenters"][1:]), 1):
            self.assertAlmostEqual(math.degrees(math.atan2(end[2] - start[2], end[0] - start[0])),
                                   15 * index, places=6,
                                   msg="each material span must take its own discrete bend angle")

    def test_shell_is_closed_and_triangle_winding_is_consistent(self):
        mesh = self.preview()["formedPreviewMesh"]
        positions, indices = mesh["positions"], mesh["indices"]
        edges = {}
        for offset in range(0, len(indices), 3):
            a, b, c = indices[offset:offset + 3]
            p = [positions[3 * i:3 * i + 3] for i in (a, b, c)]
            u = [p[1][axis] - p[0][axis] for axis in range(3)]
            v = [p[2][axis] - p[0][axis] for axis in range(3)]
            area = math.dist([0, 0, 0], [u[1] * v[2] - u[2] * v[1],
                u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]])
            self.assertGreater(area, 1e-9)
            for frm, to in ((a, b), (b, c), (c, a)):
                key = (min(frm, to), max(frm, to))
                edges.setdefault(key, []).append(1 if frm < to else -1)
        self.assertTrue(edges)
        self.assertTrue(all(sorted(direction) == [-1, 1] for direction in edges.values()),
                        "the target is one closed hollow shell with consistently oriented faces")

    def test_slot_count_radius_and_spacing_model_change_blank_and_target_together(self):
        chord = self.preview({"bendRadius": 70}, {"segmentCount": 4, "spacingModel": "chord"})
        tangent = self.preview({"bendRadius": 70}, {"segmentCount": 4, "spacingModel": "tangent"})
        for plan in (chord, tangent):
            metadata = plan["formedPreviewMesh"]["metadata"]
            self.assertEqual(metadata["segmentCount"], 4)
            self.assertEqual(metadata["stationCount"], 6)
            self.assertAlmostEqual(plan["manufacturingParts"][0]["request"]["length"],
                                   520 + metadata["patternLength"])
            self.assertAlmostEqual(math.dist(metadata["annotations"]["anchors"]["firstHinge"],
                                             metadata["annotations"]["anchors"]["secondHinge"]),
                                   metadata["slotPitch"])
            self.assertEqual(plan["parameters"], {"angle": 90, "bendRadius": 70},
                             "the script must not add derived tool values to host inputs")
        self.assertGreater(tangent["formedPreviewMesh"]["metadata"]["slotPitch"],
                           chord["formedPreviewMesh"]["metadata"]["slotPitch"])
        self.assertGreater(tangent["manufacturingParts"][0]["request"]["length"],
                           chord["manufacturingParts"][0]["request"]["length"])

    def test_tolerance_draft_controls_hinge_count(self):
        plan = self.preview(tool_values={"countMode": "tolerance", "maximumChordError": 1})
        metadata = plan["formedPreviewMesh"]["metadata"]
        self.assertGreaterEqual(metadata["segmentCount"], 2)
        self.assertNotEqual(metadata["segmentCount"], 6)
        self.assertEqual(metadata["stationCount"], metadata["segmentCount"] + 2)


if __name__ == "__main__":
    unittest.main()
