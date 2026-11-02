"""Closed-frame preparation follows the unchanged directed design member."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

from MinimalProtectiveGrilleTests import SRC, package
from ProductManufacturingDeclarationTests import execute_definition


class MinimalProtectiveGrilleSourceSpanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.module = package()
        cls.shared = sys.modules[cls.module.MODULE]

    def test_all_closed_corner_routes_map_real_prepared_extents(self):
        for corner in ('miter_45', 'horizontal_wrap', 'vertical_wrap'):
            with self.subTest(corner=corner):
                values = {**self.defaults, 'frameType': 'closed_frame',
                          'frameCornerJoint': corner, 'barLayoutMode': 'fixed_count',
                          'barCount': 8, 'handleEnabled': False}
                before = deepcopy(values)
                design = self.module.display(values)
                declarations = self.module.manufacturing(values)
                actual = execute_definition(declarations, design=design)
                self.assertEqual(values, before)
                # This executor helper builds an internal declaration model;
                # the public entry point owns the host echo.
                self.assertEqual(self.shared.generate(values, {'template': self.descriptor,
                                                              'geometryPurpose': 'manufacturing'})['parameters'], before)
                source = next(item for item in design['items'] if item['key'] == 'frame.top.0001')
                member = source['properties']['assemblyFrame.member']
                stock = next(item for item in actual['items'] if item['key'] == source['key'])
                spans = stock['properties']['manufacturing.sourceMembers']
                self.assertEqual([entry['itemKey'] for entry in spans], [source['key']])
                span = spans[0]['spans'][0]
                self.assertGreater(member['start'][0], member['end'][0])
                self.assertGreater(span['start'][0], span['end'][0])
                self.assertGreaterEqual(span['startReserve'], 0)
                self.assertGreaterEqual(span['endReserve'], 0)
                self.assertEqual(span['placement']['xAxis'], [-1.0, 0.0, 0.0])
                if corner == 'vertical_wrap':
                    width = values['frameWidth']
                    self.assertEqual(span['start'][0], values['width'] - width)
                    self.assertEqual(span['end'][0], width)
                    self.assertGreater(span['sourceRange'][0], 0)
                    self.assertLess(span['sourceRange'][1], 1)
                else:
                    self.assertEqual(span['start'], member['start'])
                    self.assertEqual(span['end'], member['end'])
                    self.assertEqual(span['sourceRange'], [0.0, 1.0])

    def test_reorientation_keeps_blank_and_world_miter_planes(self):
        # The old source is retained evidence outside the runtime/catalog.
        shared_path = SRC / 'apps/tube-designer/templates/_shared/minimal_protective_grille.py'
        old_path = SRC.parent / 'output/tests/remaining-product-format/minimal-protective-grille.before-source-span.py'
        spec = importlib.util.spec_from_file_location('grille_axis_before', shared_path)
        old = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = old
        exec(compile(old_path.read_text(encoding='utf-8-sig'), str(shared_path), 'exec'), old.__dict__)
        for corner in ('miter_45', 'horizontal_wrap', 'vertical_wrap'):
            with self.subTest(corner=corner):
                values = {**self.defaults, 'frameType': 'closed_frame', 'frameCornerJoint': corner}
                context = {'template': self.descriptor, 'geometryPurpose': 'manufacturing'}
                before = old.generate(values, context)
                after = self.shared.generate(values, context)
                original = {part.part.key: part.part for part in old._layout(values)['parts']}
                current = {part.part.key: part.part for part in self.shared._layout(values)['parts']}
                self.assertEqual(set(original), set(current))
                for key in original:
                    a, b = original[key], current[key]
                    self.assertEqual({a.start, a.end}, {b.start, b.end})
                    self.assertEqual(a.profile.contours(), b.profile.contours())
                    self.assertEqual(a.length, b.length)
                def planes(document):
                    instances = document.get('extensions', {}).get('tubeDesigner.assemblyGeometryProcesses', {}).get('instances', [])
                    output = []
                    for instance in instances:
                        matrix = instance['processInput']['parts']['stock'].get('matrix')
                        for check in instance['result'].get('checks', []):
                            if check.get('kind') != 'end-plane':
                                continue
                            origin = [matrix[4*i+3] + sum(matrix[4*i+j]*check['origin'][j] for j in range(3))
                                      for i in range(3)]
                            normal = [sum(matrix[4*i+j]*check['inwardNormal'][j] for j in range(3))
                                      for i in range(3)]
                            output.append(json.dumps({'origin': origin, 'inwardNormal': normal}, sort_keys=True))
                    return sorted(output)
                self.assertEqual(planes(before), planes(after))
                if corner == 'miter_45':
                    self.assertEqual(len(planes(after)), 8)


if __name__ == '__main__':
    unittest.main()
