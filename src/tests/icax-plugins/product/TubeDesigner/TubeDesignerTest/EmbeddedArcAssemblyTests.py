"""Embedded-arc assembly preview follows the real cutter's V envelope."""

import importlib.util
import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / "apps" / "tube-designer" / "templates" / "_shared" / "assembly_template_runtime.py"


def runtime():
    spec = importlib.util.spec_from_file_location("embedded_arc_assembly_runtime", RUNTIME)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class EmbeddedArcAssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runtime()

    def preview(self, values=None, tool_values=None):
        drafts = {"node-slot": {"embedded-arc-notch": tool_values or {}}}
        return self.runtime.preview_plan("node-embedded-arc-integrated", values or {}, drafts)

    def test_default_target_uses_actual_slot_envelope_and_hinge_root(self):
        plan = self.preview()
        blank = plan["manufacturingParts"][0]["request"]
        feature = blank["features"][0]
        mesh = plan["formedPreviewMesh"]
        data = mesh["metadata"]
        anchors = data["annotations"]["anchors"]
        self.assertEqual(plan["parameters"], {"angle": 90})
        self.assertEqual(feature["toolRef"]["id"], "embedded-arc-notch")
        self.assertEqual(data["previewKind"], "target-shape")
        self.assertEqual(data["formingValidation"], "not-performed")
        self.assertTrue(data["retainedArcIsLocalCut"])
        self.assertEqual(data["annotations"]["kind"], "embedded-arc-target")
        self.assertAlmostEqual(data["leftCutReach"], 37)
        self.assertAlmostEqual(data["rightCutReach"], 37)
        self.assertAlmostEqual(data["patternLength"], 74)
        self.assertAlmostEqual(blank["length"], 594)
        self.assertAlmostEqual(feature["station"], 0)
        self.assertEqual(anchors["hinge"], [0, 0, -17])
        self.assertAlmostEqual(math.dist(anchors["straightStart"], anchors["firstUncutEdge"]), 260)
        self.assertAlmostEqual(math.dist(anchors["lastUncutEdge"], anchors["straightEnd"]), 260)

    def test_retained_circle_is_a_local_cut_not_a_bend_radius(self):
        smaller = self.preview(tool_values={"arcRadius": 6})
        larger = self.preview(tool_values={"arcRadius": 12})
        small_mesh = smaller["formedPreviewMesh"]
        large_mesh = larger["formedPreviewMesh"]
        self.assertEqual(small_mesh["positions"], large_mesh["positions"])
        self.assertEqual(smaller["manufacturingParts"][0]["request"]["length"],
                         larger["manufacturingParts"][0]["request"]["length"])
        self.assertEqual(small_mesh["metadata"]["arcRadius"], 6)
        self.assertEqual(large_mesh["metadata"]["arcRadius"], 12)
        self.assertEqual(smaller["parameters"], {"angle": 90})
        for plan, radius in ((smaller, 6), (larger, 12)):
            note = next(item for item in plan["previewAnnotations"]
                        if item["id"] == "retained-local-arc-radius")
            self.assertEqual(note["view"], "blank")
            self.assertEqual(note["value"], radius)
            self.assertEqual({item["id"] for item in plan["previewAnnotations"]
                              if item["view"] == "finished"}, {"node-angle"})

    def test_mirror_and_male_female_drafts_shift_cut_station_but_preserve_straight_lengths(self):
        values = {"angle": 60}
        male = {"maleFemale": True, "maleFemaleSize": 2, "fitClearance": 0.2}
        right = self.preview(values, male)
        left = self.preview(values, {**male, "rightArc": False})
        for plan in (right, left):
            data = plan["formedPreviewMesh"]["metadata"]
            anchors = data["annotations"]["anchors"]
            blank = plan["manufacturingParts"][0]["request"]
            feature = blank["features"][0]
            self.assertEqual(plan["parameters"], values)
            self.assertAlmostEqual(blank["length"], 520 + data["patternLength"])
            self.assertAlmostEqual(feature["station"], data["cutStation"])
            self.assertAlmostEqual(plan["resolvedWorkflow"]["partOperations"][0]["placement"]["station"],
                                   data["cutStation"])
            self.assertAlmostEqual(math.dist(anchors["straightStart"], anchors["firstUncutEdge"]), 260)
            self.assertAlmostEqual(math.dist(anchors["lastUncutEdge"], anchors["straightEnd"]), 260)
            self.assertAlmostEqual(feature["station"] - data["leftCutReach"], -blank["length"] / 2 + 260)
            self.assertAlmostEqual(feature["station"] + data["rightCutReach"], blank["length"] / 2 - 260)
        self.assertAlmostEqual(right["formedPreviewMesh"]["metadata"]["cutStation"], -1.2)
        self.assertAlmostEqual(left["formedPreviewMesh"]["metadata"]["cutStation"], 1.2)

    def test_target_is_closed_hollow_shell_with_nonzero_triangles(self):
        mesh = self.preview()["formedPreviewMesh"]
        positions, indices = mesh["positions"], mesh["indices"]
        edges = {}
        for offset in range(0, len(indices), 3):
            a, b, c = indices[offset:offset + 3]
            p = [positions[3 * i:3 * i + 3] for i in (a, b, c)]
            u = [p[1][axis] - p[0][axis] for axis in range(3)]
            v = [p[2][axis] - p[0][axis] for axis in range(3)]
            cross = [u[1] * v[2] - u[2] * v[1],
                     u[2] * v[0] - u[0] * v[2],
                     u[0] * v[1] - u[1] * v[0]]
            self.assertGreater(math.dist([0, 0, 0], cross), 1e-9)
            for frm, to in ((a, b), (b, c), (c, a)):
                key = (min(frm, to), max(frm, to))
                edges.setdefault(key, []).append(1 if frm < to else -1)
        self.assertTrue(edges)
        self.assertTrue(all(sorted(direction) == [-1, 1] for direction in edges.values()))


if __name__ == "__main__":
    unittest.main()
