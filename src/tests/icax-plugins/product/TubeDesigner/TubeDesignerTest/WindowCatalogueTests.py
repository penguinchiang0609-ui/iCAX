import json
from copy import deepcopy
from pathlib import Path
import sys
import unittest

SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / "iCAX-Engine/framework/TemplateRuntime/python"))
from icax_template_worker import _load_template, _evaluate, _execute_manufacturing
from icax_template_sdk.manufacturing import compose_manufacturing_model


def public_preview(name, descriptor, parameters, purpose):
    """Invoke the two current product entries without a historical generate API."""
    saved = deepcopy(parameters)
    result = _evaluate({'template': descriptor, 'parameters': parameters,
        'templatePath': str(SRC / 'apps/tube-designer/templates/product' / name / 'template.py'),
        'context': {'geometryPurpose': purpose}})
    assert parameters == saved
    assert 'parameters' not in result and 'template' not in result
    return result


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
                        saved = deepcopy(p)
                        result = public_preview('single_face_security_window', d, p, purpose)
                        self.assertEqual(p, saved)
                        if purpose == "display":
                            self.assertTrue(result['items'])
                            self.assertEqual(result["schema"], "icax.display-model")
                            self.assertNotIn("parameters", result)
                            self.assertNotIn("template", result)
                        else:
                            self.assertEqual(result['schema'], 'icax.manufacturing-model')
                            design = module.display(p)
                            composed = compose_manufacturing_model(result, design)
                            self.assertTrue(composed['items'])
                            executed = _execute_manufacturing({'manufacturingDefinition': result,
                                'designModel': design, 'context': {'sharedRoot': str(SRC / 'apps/tube-designer/templates/_shared')}})
                            self.assertTrue(executed['items'])
                            self.assertEqual(executed['parameters'], {})
                            self.assertEqual(p, saved)
        with self.assertRaises(ValueError):
            public_preview('single_face_security_window', d, dict(defaults, faceType="four"), 'display')
        # A top-face draft belongs to five-face only and must not invalidate two-face.
        module.display(dict(defaults, faceType="two", accessDoorFace5="top"))
        for preset in d["extensions"]["parameterPresets"]["presets"]:
            for face in ("single", "two", "three", "five"):
                with self.subTest(preset=preset["value"], face=face):
                    values = dict(defaults, **preset['values'], faceType=face)
                    declaration = public_preview('single_face_security_window', d, values, 'manufacturing')
                    self.assertTrue(compose_manufacturing_model(declaration, module.display(values))['items'])

    @unittest.skip("Deferred product reference; no current active product generation")
    def test_louver_orientation_angles_and_collision(self):
        d, defaults, module = package("louver_window")
        for direction in (0, 90, 30, -45):
            for angle in (-90, -45, 0, 45, 90):
                for purpose in ("display", "manufacturing"):
                    with self.subTest(direction=direction, angle=angle, purpose=purpose):
                        values = dict(defaults, bladeDirectionAngle=direction, bladeRollAngle=angle, bladePitch=100)
                        saved = deepcopy(values)
                        result = (module.display(values) if purpose == "display" else
                                  module.generate(values, {"template": d, "geometryPurpose": purpose}))
                        self.assertEqual(values, saved)
                        if purpose == "display":
                            self.assertEqual(result["schema"], "icax.display-model")
                            self.assertNotIn("parameters", result)
                        else:
                            self.assertEqual(result["parameters"], values)
                        self.assertGreater(len(result["items"]), 4)
        for changes in ({"bladeRollAngle": 0, "bladePitch": 10}, {"bladePitch": 0}, {"bladeRollAngle": 100},
                        {"bladeWallThickness": 100}, {"width": 50}, {"bladePitch": 1, "height": 10000}):
            with self.assertRaises(ValueError):
                module.generate(dict(defaults, **changes), {"template": d})
