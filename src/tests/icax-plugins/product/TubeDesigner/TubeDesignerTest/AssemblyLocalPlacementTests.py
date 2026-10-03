"""Actual local faces, rotated envelopes and matching hole patterns."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[6]
SPEC = importlib.util.spec_from_file_location("local_placement_geometry",
    ROOT / "src/apps/tube-designer/templates/_shared/assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(geometry)


def part(face="top", station=100, rotation=0):
    return {"length": 200, "profileRef": {"scope": "system", "id": "rect"},
        "parameters": {"width": 60, "depth": 40, "wallThickness": 2},
        "matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
        "anchor": {"kind": "side", "reference": "start", "face": face,
                   "station": station, "offset": 0, "rotation": rotation}}


class AssemblyLocalPlacementTests(unittest.TestCase):
    def test_native_xyz_face_convention_and_real_wall_separation(self):
        a, b = part(), part()
        b["matrix"][11] = 40
        self.assertEqual(geometry.site(a)["normal"], [0, 0, 1])
        self.assertEqual(geometry.site(part("left"))["normal"], [0, 1, 0])
        geometry.aligned_hole_sites(a, b, 0.1)
        b["matrix"][7] = 1
        with self.assertRaisesRegex(ValueError, "实际孔位"):
            geometry.aligned_hole_sites(a, b, 0.1)

    def test_rotation_checks_projected_bounds_without_inflating_a_round_hole(self):
        geometry.local_envelope(part(station=7, rotation=45), 12, 12, "圆孔", radius=6)
        with self.assertRaisesRegex(ValueError, "长度"):
            geometry.local_envelope(part(station=7, rotation=45), 12, 12, "矩形孔")
        geometry.local_envelope(part(rotation=0), 50, 12, "孔组", radius=6)
        with self.assertRaisesRegex(ValueError, "实际加工侧面"):
            geometry.local_envelope(part("left", rotation=90), 50, 12, "孔组", radius=6)

    def test_arrays_require_matching_actual_directions_but_single_holes_do_not(self):
        a, b = part(), part(rotation=90)
        geometry.aligned_hole_sites(a, b, 0.1, count=1)
        with self.assertRaisesRegex(ValueError, "排列方向"):
            geometry.aligned_hole_sites(a, b, 0.1, count=2)
        b["anchor"]["rotation"] = 180
        geometry.aligned_hole_sites(a, b, 0.1, count=2)

    def test_adjustment_range_follows_rotated_slot_axis(self):
        a, b = part(rotation=90), part(rotation=90)
        before = copy.deepcopy((a, b))
        b["matrix"][7] = 4
        geometry.aligned_hole_sites(a, b, 0.1, travel=10)
        b["matrix"][3] = 4
        with self.assertRaisesRegex(ValueError, "实际孔位"):
            geometry.aligned_hole_sites(a, b, 0.1, travel=10)
        self.assertEqual(a, before[0], "geometric checks do not rewrite target pose or processing anchors")


if __name__ == "__main__":
    unittest.main()
