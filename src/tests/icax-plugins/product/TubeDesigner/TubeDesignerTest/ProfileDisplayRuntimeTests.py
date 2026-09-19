"""All shipped profile displays are package-local and geometry-neutral."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'src/apps/tube-designer/templates').is_dir())
TEMPLATES = ROOT / 'src/apps/tube-designer/templates'
spec = importlib.util.spec_from_file_location('profile_display_runtime', TEMPLATES / '_shared/profile_package_runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)

class ProfileDisplayTests(unittest.TestCase):
    def test_all_profiles_load_display_without_changing_geometry_or_inputs(self):
        packages = runtime.generate({'action':'list-system', 'profileRoot':str(TEMPLATES / 'profile')}, {})['systemProfiles']
        self.assertEqual(21, len(packages))
        for package in packages:
            descriptor = package['descriptor']
            with self.subTest(profile=descriptor['id']):
                self.assertNotIn('error', package)
                source = json.loads((TEMPLATES / 'profile' / descriptor['id'] / 'profile.json').read_text(encoding='utf-8'))
                self.assertNotIn('parameterDiagram', source)
                display = descriptor['display']
                self.assertEqual('icax.template-display', display['schema'])
                keys = {
                    d['key'] for d in descriptor['parameters']
                    if d.get('presentation', {}).get('visible', True)
                }
                self.assertEqual(keys, set(display['views']['right']['fields']))
                self.assertTrue(set(display['views']['scene']['annotations']) <= keys)
                self.assertEqual(package['defaultParameters'], package['previewProfile']['parameters'])
                plain = copy.deepcopy(descriptor)
                plain.pop('display')
                generated = runtime._evaluate(plain, package['scriptSource'], package['defaultParameters'], '', '', package['resources'])
                self.assertTrue(generated.get('parameterDiagram', {}).get('annotations'))
                resources = {k:v for k,v in package['resources'].items() if k != 'display.json'}
                geometry = runtime._evaluate(plain, package['scriptSource'], package['defaultParameters'], '', '', resources)
                self.assertEqual(geometry['contours'], generated['contours'])
                self.assertEqual(geometry['parameters'], generated['parameters'])

if __name__ == '__main__':
    unittest.main()
