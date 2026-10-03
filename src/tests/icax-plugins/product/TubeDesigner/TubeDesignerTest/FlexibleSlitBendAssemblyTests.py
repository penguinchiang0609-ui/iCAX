"""The flexible target sweep must follow the actual slit tool's layout."""

import importlib.util
import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps" / "tube-designer" / "templates" / "_shared" / "assembly_template_runtime.py"


def runtime():
    spec = importlib.util.spec_from_file_location("flexible_assembly_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FlexibleSlitBendAssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runtime()

    def preview(self, values=None, tool_values=None):
        drafts = {"node-slot": {"flexible-slit-bend": tool_values or {}}}
        return self.runtime.preview_plan("flexible-slit-bend-integrated", values or {}, drafts)

    def test_target_uses_tool_pitch_and_full_curved_zone(self):
        plan = self.preview()
        mesh = plan["formedPreviewMesh"]
        metadata = mesh["metadata"]
        flexible_length = math.pi * 40 / 2
        pitch = flexible_length / 7
        pattern = 5 * pitch + 1
        self.assertEqual(metadata["previewKind"], "target-shape")
        self.assertEqual(metadata["formingValidation"], "calibration-required")
        self.assertEqual(metadata["bendProcess"], "flexible-slit-bend")
        self.assertEqual(metadata["slitMode"], "narrow")
        self.assertEqual(metadata["slitCount"], 6)
        self.assertAlmostEqual(metadata["slitPitch"], pitch)
        self.assertAlmostEqual(metadata["flexibleLength"], flexible_length)
        self.assertAlmostEqual(metadata["patternLength"], pattern)
        self.assertAlmostEqual(metadata["patternMargin"], (flexible_length - pattern) / 2)
        self.assertAlmostEqual(plan["manufacturingParts"][0]["request"]["length"],
                               520 + flexible_length)
        self.assertEqual(plan["parameters"], {"angle": 90, "bendRadius": 40})
        notes = {item["id"]: item for item in plan["previewAnnotations"]
                 if item["kind"] == "value-note"}
        self.assertAlmostEqual(notes["target-centerline-radius"]["value"], 40)
        self.assertAlmostEqual(notes["target-flexible-length"]["value"], flexible_length)
        self.assertEqual(notes["target-centerline-radius"]["view"], "blank")
        self.assertEqual(notes["target-flexible-length"]["view"], "blank")
        self.assertEqual({item["id"] for item in notes.values() if item["view"] == "finished"},
                         {"node-angle"})
        self.assertEqual(len(plan["manufacturingParts"]), 1)
        feature = plan["manufacturingParts"][0]["request"]["features"][0]
        self.assertEqual(feature["toolRef"]["id"], "flexible-slit-bend")
        self.assertEqual(feature["station"], 0)
        anchors = metadata["annotations"]["anchors"]
        self.assertEqual(len(anchors["slitCenters"]), 6)
        self.assertAlmostEqual(math.dist(anchors["straightStart"], anchors["curveStart"]), 260)
        self.assertAlmostEqual(math.dist(anchors["curveEnd"], anchors["straightEnd"]), 260)
        self.assertAlmostEqual(anchors["curveEnd"][0], 40)
        self.assertAlmostEqual(anchors["curveEnd"][2], 40)
        for index, center in enumerate(anchors["slitCenters"], 1):
            theta = index * pitch / 40
            self.assertAlmostEqual(center[0], 40 * math.sin(theta))
            self.assertAlmostEqual(center[2], 40 * (1 - math.cos(theta)))

    def test_target_is_a_closed_non_degenerate_hollow_shell(self):
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
        self.assertTrue(all(sorted(direction) == [-1, 1] for direction in edges.values()))

    def test_tool_drafts_change_target_slit_witnesses_without_changing_host_inputs(self):
        values = {"angle": 75, "bendRadius": 60}
        base = self.preview(values, {"slitCount": 4, "slitMode": "straight"})
        narrow = self.preview(values, {"slitCount": 4, "slitMode": "narrow"})
        u = self.preview(values, {"slitCount": 4, "slitMode": "u", "uSpan": 8})
        denser = self.preview(values, {"slitCount": 8, "slitMode": "straight"})
        for plan in (base, narrow, u, denser):
            self.assertEqual(plan["parameters"], values)
            metadata = plan["formedPreviewMesh"]["metadata"]
            self.assertAlmostEqual(plan["manufacturingParts"][0]["request"]["length"],
                                   520 + 60 * math.radians(75))
            self.assertEqual(len(metadata["annotations"]["anchors"]["slitCenters"]),
                             metadata["slitCount"])
        self.assertNotEqual(base["formedPreviewMesh"]["positions"],
                            narrow["formedPreviewMesh"]["positions"])
        self.assertNotEqual(base["formedPreviewMesh"]["positions"],
                            u["formedPreviewMesh"]["positions"])
        self.assertNotEqual(base["formedPreviewMesh"]["positions"],
                            denser["formedPreviewMesh"]["positions"])
        self.assertGreater(u["formedPreviewMesh"]["metadata"]["patternLength"],
                           base["formedPreviewMesh"]["metadata"]["patternLength"])

    def test_angle_and_radius_change_centerline_geometry(self):
        baseline = self.preview()
        larger_radius = self.preview({"bendRadius": 70})
        smaller_angle = self.preview({"angle": 60})
        baseline_anchor = baseline["formedPreviewMesh"]["metadata"]["annotations"]["anchors"]["curveEnd"]
        radius_anchor = larger_radius["formedPreviewMesh"]["metadata"]["annotations"]["anchors"]["curveEnd"]
        angle_anchor = smaller_angle["formedPreviewMesh"]["metadata"]["annotations"]["anchors"]["curveEnd"]
        self.assertNotEqual(baseline_anchor, radius_anchor)
        self.assertNotEqual(baseline_anchor, angle_anchor)


if __name__ == "__main__":
    unittest.main()
