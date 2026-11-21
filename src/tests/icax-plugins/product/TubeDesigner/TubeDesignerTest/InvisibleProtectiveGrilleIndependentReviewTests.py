"""Physical-stock and shared-executor checks for the single-plane wire grille.

These tests inspect the actual extrusion resources rather than trusting layout
facts. Native BRep, scene persistence and desktop acceptance remain separate.
"""
from copy import deepcopy
from itertools import combinations, product
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


SRC = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(SRC / 'iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_worker import _evaluate, _execute_manufacturing, _load_template
from icax_template_sdk import to_display_model
from icax_template_sdk.display import expand_display_model
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model
from icax_template_sdk.profile_constraints import _profile_validation_scope


DIRECTORY = SRC / 'apps/tube-designer/templates/product/invisible_protective_grille'
SHARED = SRC / 'apps/tube-designer/templates/_shared'
DESCRIPTOR = json.loads((DIRECTORY / 'template.json').read_text(encoding='utf-8'))
DEFAULTS = {field['key']: field['defaultValue'] for field in DESCRIPTOR['parameters']}
MODULE = _load_template(str(DIRECTORY / 'template.py'), 'invisible-grille-physical-stock-tests')
BOUNDARY = MODULE._load(SHARED / 'profile_recognition_geometry.py')
TOLERANCE = 1.e-7


def placed(point, frame):
    origin = frame.get('origin', [0., 0., 0.])
    x, y = frame.get('xAxis', [1., 0., 0.]), frame.get('yAxis', [0., 1., 0.])
    z = frame.get('zAxis', [x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2], x[0]*y[1]-x[1]*y[0]])
    return tuple(origin[i]+sum(point[j]*axis[i] for j, axis in enumerate((x, y, z))) for i in range(3))


def displayed_stocks(document):
    """Recover actual section envelopes and axis endpoints from display resources."""
    expand_display_model(document)
    resources = {resource['key']: resource for resource in document['resources']}
    result = []
    for item in document['items']:
        instance = item['geometry']
        extrusion = resources[instance['resource']]
        if extrusion['operator'] != 'extrude':
            raise AssertionError('Each wire/strip must remain an uncut physical stock')
        section = resources[extrusion['inputs'][0]]
        if section['operator'] != 'profile2d':
            raise AssertionError('Every stock must retain its intrinsic section')
        contours = section['arguments']['contours']
        if len(contours) != 1:
            raise AssertionError('Wire and strips must both use a real solid section')
        normalized = BOUNDARY.contour(contours[0], TOLERANCE)
        u0, v0, u1, v1 = BOUNDARY.bounds(normalized)
        vector, frame = extrusion['arguments']['vector'], instance['placement']
        local_frame = section['arguments'].get('placement', {})
        points = []
        for u, v, end in product((u0, u1), (v0, v1), (0, 1)):
            point = placed([u, v, 0.], local_frame)
            points.append(placed([point[i]+end*vector[i] for i in range(3)], frame))
        result.append({'key': item['key'], 'item': item, 'section': normalized,
            'start': placed([0., 0., 0.], frame), 'end': placed(vector, frame),
            'bounds': tuple((min(p[i] for p in points), max(p[i] for p in points)) for i in range(3))})
    return result


class InvisibleProtectiveGrilleIndependentReviewTests(unittest.TestCase):
    def values(self, **changes):
        return {**DEFAULTS, **changes}

    def display(self, values):
        before, documents = deepcopy(values), []

        def capture(document):
            documents.append(deepcopy(document))
            return to_display_model(document)

        with _profile_validation_scope(DESCRIPTOR), patch.object(MODULE, 'to_display_model', capture):
            result = MODULE.display(values)
        self.assertEqual(values, before)
        self.assertEqual(len(documents), 1)
        self.assertEqual(documents[0]['parameters'], before,
                         'Derived stock/layout fields must never leak into normalized inputs')
        self.assertNotIn('parameters', result)
        return result

    def execute(self, values):
        before = deepcopy(values)
        with _profile_validation_scope(DESCRIPTOR):
            state = MODULE.layout(values)
            design = self.display(values)
            declaration = MODULE.manufacturing(values)
        frozen_design, frozen_declaration = deepcopy(design), deepcopy(declaration)
        validate_manufacturing_model(declaration)
        composed = compose_manufacturing_model(declaration, design)
        self.assertEqual({m['itemKey'] for m in composed['sourceMappings']},
                         {m['key'] for m in state['members']})
        actual = _execute_manufacturing({'manufacturingDefinition': declaration, 'designModel': design,
                                        'context': {'sharedRoot': str(SHARED)}})
        self.assertEqual(values, before)
        self.assertEqual(design, frozen_design)
        self.assertEqual(declaration, frozen_declaration)
        prepared = {entry['itemKey']: entry for entry in
                    actual['extensions']['tubeDesigner.workpiecePreparations']}
        return state, design, declaration, actual, prepared

    def assert_physical_layout(self, values, design):
        stocks = displayed_stocks(design)
        wires = [s for s in stocks if s['key'].startswith('wire.')]
        strips = [s for s in stocks if s['key'].startswith('clamp.')]
        self.assertTrue(wires)
        self.assertEqual(len(strips), 2*(values['clampCount']+2))
        self.assertEqual(len(stocks), len(wires)+len(strips))
        wire_profile = wires[0]['item']['properties']['tubeDesigner.profile']
        clamp_profile = strips[0]['item']['properties']['tubeDesigner.profile']
        diameter, band, thickness = wire_profile['width'], clamp_profile['width'], clamp_profile['depth']
        centers = []
        for wire in wires:
            x0, x1 = wire['bounds'][0]
            centers.append((x0+x1)/2)
            self.assertEqual(wire['section']['kind'], 'circle')
            self.assertAlmostEqual(wire['section']['radius'], diameter/2)
            self.assertEqual(wire['start'], (wire['start'][0], 0., 0.))
            self.assertEqual(wire['end'], (wire['start'][0], 0., values['height']))
            self.assertAlmostEqual(wire['bounds'][1][0], -diameter/2)
            self.assertAlmostEqual(wire['bounds'][1][1], diameter/2)
            self.assertEqual(wire['bounds'][2], (0., values['height']))
            self.assertGreaterEqual(x0, -TOLERANCE)
            self.assertLessEqual(x1, values['width']+TOLERANCE)
        centers.sort()
        self.assertAlmostEqual(centers[0], values['width']-centers[-1])
        self.assertEqual(len(wires), math.floor((values['width']-diameter)/values['wireCenterSpacing'])+1)
        for left, right in zip(centers, centers[1:]):
            self.assertAlmostEqual(right-left, values['wireCenterSpacing'])
        groups = {}
        for strip in strips:
            key, side = strip['key'].rsplit('.', 1)
            groups.setdefault(key, {})[side] = strip
            self.assertEqual(strip['bounds'][0], (0., values['width']))
            self.assertAlmostEqual(strip['bounds'][2][1]-strip['bounds'][2][0], band)
            self.assertAlmostEqual(strip['bounds'][1][1]-strip['bounds'][1][0], thickness)
            self.assertGreaterEqual(strip['bounds'][2][0], -TOLERANCE)
            self.assertLessEqual(strip['bounds'][2][1], values['height']+TOLERANCE)
        for group in groups.values():
            self.assertEqual(set(group), {'front', 'back'})
            self.assertEqual(group['front']['bounds'][2], group['back']['bounds'][2])
            self.assertAlmostEqual(group['front']['bounds'][1][1], -diameter/2)
            self.assertAlmostEqual(group['back']['bounds'][1][0], diameter/2)
            self.assertAlmostEqual(group['front']['start'][1], -group['back']['start'][1])
        self.assertEqual(groups['clamp.bottom']['front']['bounds'][2], (0., band))
        self.assertEqual(groups['clamp.top']['front']['bounds'][2], (values['height']-band, values['height']))
        for first, second in combinations(stocks, 2):
            self.assertFalse(all(min(a[1], b[1])-max(a[0], b[0]) > TOLERANCE
                                 for a, b in zip(first['bounds'], second['bounds'])),
                             first['key']+' overlaps '+second['key'])
        return wires, strips

    def test_default_real_display_has_centered_continuous_wires_and_three_strip_pairs(self):
        values = self.values()
        wires, strips = self.assert_physical_layout(values, self.display(values))
        self.assertEqual((len(wires), len(strips)), (24, 6))
        self.assertEqual([w['start'][0] for w in wires], [25.+50*i for i in range(24)])

    def test_zero_one_and_many_middle_groups_are_real_pairs_on_one_plane(self):
        for count, bottom, top, expected in (
                (0, 10000., 10000., []), (1, 410., 10000., [410.]),
                (2, 150., 250., [150., 950.]), (4, 150., 150., [150., 450., 750., 1050.])):
            with self.subTest(count=count):
                values = self.values(clampCount=count, clampBottomOffset=bottom, clampTopOffset=top)
                design = self.display(values)
                self.assert_physical_layout(values, design)
                actual = sorted(item['geometry']['placement']['origin'][2] for item in design['items']
                                if item['key'].startswith('clamp.middle.') and item['key'].endswith('.front'))
                self.assertEqual(actual, expected)

    def test_size_and_selected_section_dimensions_drive_real_stock_envelopes(self):
        for width, height, diameter, band, thickness, pitch in (
                (100., 100., 1., 5., .5, 30.), (1600., 1800., 3., 35., 6., 60.),
                (2180., 970., 5., 25., 2., 45.)):
            with self.subTest(width=width, height=height):
                values = self.values(width=width, height=height, wireWidth=diameter,
                    clampWidth=band, clampDepth=thickness, wireCenterSpacing=pitch,
                    clampCount=0)
                self.assert_physical_layout(values, self.display(values))

    def test_shared_manufacturing_executor_preserves_whole_wire_and_two_strips_per_group(self):
        for count in (0, 1, 3):
            with self.subTest(count=count):
                values = self.values(clampCount=count, clampBottomOffset=200., clampTopOffset=200.)
                state, design, declaration, actual, prepared = self.execute(values)
                self.assert_physical_layout(values, design)
                keys = {member['key'] for member in state['members']}
                self.assertEqual(set(prepared), keys)
                self.assertEqual({item['key'] for item in actual['items']}, keys)
                self.assertEqual(len(declaration['processes']), len(keys))
                self.assertTrue(all(p['definition']['templateId'] == 'profile-stock-preparation'
                                    for p in declaration['processes']))
                for member in state['members']:
                    key = member['key']
                    expected = values['height'] if key.startswith('wire.') else values['width']
                    self.assertTrue(prepared[key]['ready'])
                    self.assertEqual(prepared[key]['status'], 'prepared')
                    self.assertAlmostEqual(prepared[key]['physicalLength'], expected)
                    self.assertEqual(prepared[key]['axialInterval'], {'startStation': 0., 'endStation': expected})
                self.assertTrue(declaration['connections'])
                self.assertTrue(all(c['kind'] == 'assembly' for c in declaration['connections']))
                self.assertEqual(len(declaration['connections']), state['facts']['wireCount']*(count+2))
                self.assertFalse(actual['extensions'].get('tubeDesigner.assemblyGeometryProcesses'))

    def test_profile_library_snapshots_keep_exact_identity_and_selected_geometry(self):
        catalogue = MODULE._load(SHARED / 'tube_profile_catalog.py')
        selected = self.values(wireWidth=4., clampWidth=28., clampDepth=5.)
        snapshots = {}
        for role in ('wire', 'clamp'):
            profile = catalogue.load_profile(selected, role)
            snapshots[role] = {'schema': 'icax.imported-tube-profile', 'schemaVersion': 1,
                'kind': 'profile-package', 'profileForm': 'parametric', 'sectionKind': profile.kind,
                'contentDigest': 'user-'+role+'-selected-section', 'sourceFileName': role+'.icaxprofile',
                'name': '我的'+role, 'packageVersion': '1.0.0', 'width': profile.width,
                'depth': profile.depth, 'wallThickness': 0., 'hollow': False, 'contourCount': 1,
                'contours': profile.contours(), 'specification': 'selected '+role}
        # Resource snapshots, rather than stale fallback parameter values,
        # control actual section geometry in the ordinary library path.
        values = self.values(tubeDesignerProfileOverrides=snapshots)
        before = deepcopy(values)
        _, design, _, actual, prepared = self.execute(values)
        self.assert_physical_layout(values, design)
        self.assertEqual(values, before)
        for item in design['items']:
            role = item['properties']['tubeDesigner.productRule']['profileRole']
            profile = item['properties']['tubeDesigner.profile']
            self.assertEqual(profile['contentDigest'], snapshots[role]['contentDigest'])
            self.assertEqual(profile['contours'], snapshots[role]['contours'])
            self.assertEqual(profile['width'], snapshots[role]['width'])
            self.assertEqual(profile['depth'], snapshots[role]['depth'])
        self.assertEqual(len(actual['items']), len(prepared))

    def test_public_worker_entries_keep_complete_normalized_input_unchanged(self):
        for count in (0, 1, 3):
            values = self.values(clampCount=count, clampBottomOffset=200., clampTopOffset=200.)
            before = deepcopy(values)
            for purpose, schema in (('display', 'icax.display-model'), ('manufacturing', 'icax.manufacturing-model')):
                with self.subTest(count=count, purpose=purpose):
                    result = _evaluate({'template': DESCRIPTOR, 'parameters': values,
                        'templatePath': str(DIRECTORY / 'template.py'), 'context': {'geometryPurpose': purpose}})
                    self.assertEqual(values, before)
                    self.assertEqual(result['schema'], schema)
                    self.assertNotIn('parameters', result)
                    self.assertNotIn('template', result)

    def test_invalid_spacing_counts_and_overlapping_groups_fail_without_mutating_inputs(self):
        invalid = ({'wireCenterSpacing': 0}, {'wireCenterSpacing': 2}, {'wireCenterSpacing': 1.5},
            {'wireCenterSpacing': float('nan')}, {'wireCenterSpacing': float('inf')},
            {'clampCount': -1}, {'clampCount': 101}, {'clampCount': 1.5}, {'clampCount': True},
            {'clampBottomOffset': 0}, {'clampBottomOffset': 30}, {'clampBottomOffset': 1200},
            {'clampCount': 2, 'clampBottomOffset': 600, 'clampTopOffset': 600},
            {'clampCount': 2, 'clampBottomOffset': 800, 'clampTopOffset': 600},
            {'clampCount': 3, 'clampBottomOffset': 100, 'clampTopOffset': 1080},
            {'height': 100, 'clampWidth': 50}, {'width': 10000, 'wireWidth': 1, 'wireCenterSpacing': 2},
            {'width': 99}, {'height': 10001}, {'wireProfileType': 'round'}, {'clampProfileType': 'rect'})
        for changes in invalid:
            with self.subTest(changes=changes):
                values = self.values(**changes)
                before = deepcopy(values)
                with _profile_validation_scope(DESCRIPTOR), self.assertRaises(ValueError):
                    MODULE.manufacturing(values)
                self.assertEqual(values, before)

    def test_inapplicable_profile_snapshots_are_rejected_by_actual_resource_consumption(self):
        snapshot = {'schema': 'icax.imported-tube-profile', 'schemaVersion': 1,
            'kind': 'profile-package', 'profileForm': 'fixed', 'sectionKind': 'rect',
            'width': 10., 'depth': 10., 'wallThickness': 1., 'hollow': True, 'contourCount': 2,
            'contours': [{'kind': 'circle', 'radius': 5.}, {'kind': 'circle', 'radius': 4.}]}
        for role in ('wire', 'clamp'):
            with self.subTest(role=role):
                values = self.values(tubeDesignerProfileOverrides={role: snapshot})
                with _profile_validation_scope(DESCRIPTOR), self.assertRaises(ValueError):
                    MODULE.display(values)


if __name__ == '__main__':
    unittest.main()
