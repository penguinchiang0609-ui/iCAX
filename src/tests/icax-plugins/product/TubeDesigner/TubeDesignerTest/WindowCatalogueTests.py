import json
from pathlib import Path
import sys
import unittest

SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template


def package(name):
    directory = SRC / "apps/tube-designer/templates/product" / name
    d = json.loads((directory / "template.json").read_text(encoding="utf-8"))
    return d, {p["key"]: p["defaultValue"] for p in d["parameters"]}, _load_template(str(directory / "template.py"), "window-test-" + name)


class WindowCatalogueTests(unittest.TestCase):
    def test_all_faces_opening_positions_and_purposes(self):
        d, defaults, module = package("single_face_security_window")
        for face in ("single", "two", "three", "five"):
            for opening in (False, True):
                for purpose in ("display", "manufacturing"):
                    with self.subTest(face=face, opening=opening, purpose=purpose):
                        p = dict(defaults, faceType=face, accessDoorEnabled=opening)
                        result = module.generate(p, {"template": d, "geometryPurpose": purpose})
                        self.assertTrue(result["items"])
                        self.assertEqual(result["parameters"], p)
        with self.assertRaises(ValueError):
            module.generate(dict(defaults, faceType="four"), {"template": d})
        # A top-face draft belongs to five-face only and must not invalidate two-face.
        module.generate(dict(defaults, faceType="two", accessDoorFace5="top"), {"template": d, "geometryPurpose": "display"})
        for preset in d["extensions"]["parameterPresets"]["presets"]:
            for face in ("single", "two", "three", "five"):
                with self.subTest(preset=preset["value"], face=face):
                    module.generate(dict(defaults, **preset["values"], faceType=face), {"template": d, "geometryPurpose": "manufacturing"})

    def test_louver_orientation_angles_and_collision(self):
        d, defaults, module = package("louver_window")
        for direction in (0, 90, 30, -45):
            for angle in (-90, -45, 0, 45, 90):
                for purpose in ("display", "manufacturing"):
                    with self.subTest(direction=direction, angle=angle, purpose=purpose):
                        result = module.generate(dict(defaults, bladeDirectionAngle=direction, bladeRollAngle=angle, bladePitch=100), {"template": d, "geometryPurpose": purpose})
                        self.assertGreater(len(result["items"]), 4)
        for changes in ({"bladeRollAngle": 0, "bladePitch": 10}, {"bladePitch": 0}, {"bladeRollAngle": 100},
                        {"bladeWallThickness": 100}, {"width": 50}, {"bladePitch": 1, "height": 10000}):
            with self.assertRaises(ValueError):
                module.generate(dict(defaults, **changes), {"template": d})
