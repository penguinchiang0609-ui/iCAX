"""Physical wire/strip stocks, actual resource geometry and manufacturing flow."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package, public_preview
from ProductManufacturingDeclarationTests import (
    assert_declaration, canonical_geometry, execute_definition, forbid_processing)
from icax_template_sdk.profile_constraints import _profile_validation_scope


class InvisibleGrilleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('invisible_protective_grille')

    def state(self, **changes):
        with _profile_validation_scope(self.descriptor):
            return self.template.layout({**self.defaults, **changes})

    def execute(self, **changes):
        values = {**self.defaults, **changes}
        original = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            design = self.template.display(values)
            with forbid_processing():
                declaration = self.template.manufacturing(values)
            assert_declaration(self, declaration, design)
            result = execute_definition(declaration, design=design)
        self.assertEqual(values, original)
        return design, declaration, result

    def test_default_has_24_continuous_wires_and_six_real_strips_without_side_frame(self):
        state = self.state()
        members = state['members']
        wires = [m for m in members if m['profileRole'] == 'wire']
        strips = [m for m in members if m['profileRole'] == 'clamp']
        self.assertEqual((len(wires), len(strips), len(members)), (24, 6, 30))
        self.assertEqual([m['key'] for m in wires], [f'wire.vertical.{i:04d}' for i in range(1, 25)])
        self.assertEqual({m['role'] for m in strips}, {'clamp.bottom', 'clamp.middle', 'clamp.top'})
        self.assertFalse(state['facts']['hasSideFrame'])
        self.assertEqual(state['facts']['clampGroupCenters'], [10., 500., 1190.])
        self.assertEqual((wires[0]['start'][0], wires[-1]['start'][0]), (25., 1175.))
        for wire in wires:
            self.assertEqual(wire['start'][1:], (0., 0.))
            self.assertEqual(wire['end'][1:], (0., 1200.))
            self.assertEqual(wire['start'][0], wire['end'][0])
        self.assertEqual({math.dist(m['start'], m['end']) for m in members}, {1200.})

    def test_default_profiles_are_actual_solid_circle_and_rectangle(self):
        design, _, result = self.execute()
        self.assertEqual(len(design['items']), 30)
        for item in result['items']:
            properties = item['properties']
            wire = item['key'].startswith('wire.')
            profile = properties['tubeDesigner.profile']
            self.assertEqual(profile['kind'], 'round-bar' if wire else 'rect-bar')
            self.assertEqual(profile['wallThickness'], 0.)
            self.assertFalse(profile['hollow'])
            self.assertEqual(len(profile['contours']), 1)
            contour = profile['contours'][0]
            self.assertTrue(contour['closed'])
            self.assertEqual(len(contour['segments']), 4)
            self.assertEqual({s['kind'] for s in contour['segments']}, {'arc' if wire else 'line'})
            points = [s['start'] for s in contour['segments']]
            if wire:
                for point in points:
                    self.assertAlmostEqual(math.hypot(*point), 1.)
            else:
                self.assertEqual(set(map(tuple, points)), {(-10., -1.5), (10., -1.5), (10., 1.5), (-10., 1.5)})

    def test_strip_inner_faces_touch_wire_at_both_sides_with_no_volume_overlap(self):
        for changes in ({}, {'wireWidth': 4., 'clampWidth': 30., 'clampDepth': 6.,
                              'clampCount': 3, 'clampBottomOffset': 200., 'clampTopOffset': 200.}):
            with self.subTest(changes=changes):
                state = self.state(**changes)
                wires = [m for m in state['members'] if m['profileRole'] == 'wire']
                diameter = wires[0]['profile'].width
                for strip in (m for m in state['members'] if m['profileRole'] == 'clamp'):
                    y = strip['start'][1]
                    self.assertAlmostEqual(abs(y)-strip['profile'].depth/2, diameter/2)
                    self.assertEqual(strip['start'][0], 0.)
                    self.assertEqual(strip['end'][0], self.defaults['width'])
                self.assertEqual(len(state['connections']), len(wires)*state['facts']['clampGroupCount'])
                for relation in state['connections']:
                    self.assertEqual(relation['kind'], 'assembly')
                    self.assertEqual(len(relation['items']), 3)
                    self.assertEqual([p[1] for p in relation['properties']['contactPoints']], [-diameter/2, diameter/2])
                    self.assertTrue(relation['properties']['wireContinuous'])
                    self.assertFalse(relation['properties']['fasteningVerified'])

    def test_width_pitch_and_diameter_use_actual_center_spacing_and_centered_remainders(self):
        for width, pitch, diameter in ((1200., 80., 2.), (967., 37., 4.), (100., 200., 8.)):
            with self.subTest(width=width, pitch=pitch, diameter=diameter):
                wires = [m for m in self.state(width=width, wireCenterSpacing=pitch,
                                               wireWidth=diameter)['members'] if m['profileRole'] == 'wire']
                self.assertEqual(len(wires), math.floor((width-diameter)/pitch)+1)
                xs = [m['start'][0] for m in wires]
                for a, b in zip(xs, xs[1:]):
                    self.assertAlmostEqual(b-a, pitch)
                self.assertAlmostEqual(xs[0]-diameter/2, width-xs[-1]-diameter/2)
                self.assertGreaterEqual(xs[0]-diameter/2, 0.)

    def test_middle_groups_use_active_offsets_and_leave_inactive_drafts_unchanged(self):
        zero = self.state(clampCount=0, clampBottomOffset=-9999., clampTopOffset=9999.)
        self.assertEqual(zero['facts']['clampGroupCenters'], [10., 1190.])
        one = self.state(clampCount=1, clampBottomOffset=300., clampTopOffset=-9999.)
        self.assertEqual(one['facts']['middleClampCenters'], [300.])
        three = self.state(clampCount=3, clampBottomOffset=200., clampTopOffset=300.)
        self.assertEqual(three['facts']['middleClampCenters'], [200., 550., 900.])

    def test_extra_middle_strips_do_not_split_change_or_extend_wire_stock(self):
        before_design, _, before = self.execute(clampCount=0)
        after_design, _, after = self.execute(clampCount=3, clampBottomOffset=200., clampTopOffset=200.)
        for first, second in ((before_design, after_design), (before, after)):
            first_hashes, second_hashes = canonical_geometry(first), canonical_geometry(second)
            wires = {key: value for key, value in first_hashes.items() if key.startswith('wire.')}
            self.assertEqual(len(wires), 24)
            self.assertEqual(wires, {key: value for key, value in second_hashes.items() if key.startswith('wire.')})
        self.assertEqual((len(before['items']), len(after['items'])), (28, 34))

    def test_every_physical_stock_keeps_independent_manufacturing_source_and_length(self):
        design, declaration, result = self.execute(width=1350., height=1450., clampCount=2,
                                                   clampBottomOffset=400., clampTopOffset=350.)
        keys = {item['key'] for item in design['items']}
        self.assertEqual(len(keys), 35)
        self.assertEqual(keys, {item['key'] for item in result['items']})
        self.assertEqual(len(declaration['processes']), len(keys))
        self.assertEqual({p['definition']['templateId'] for p in declaration['processes']}, {'profile-stock-preparation'})
        for item in result['items']:
            properties = item['properties']
            expected = 1450. if item['key'].startswith('wire.') else 1350.
            self.assertEqual(properties['quantity'], 1)
            self.assertEqual(properties['length'], expected)
            self.assertEqual(properties['tubeDesigner.assemblyProcessStockLength'], expected)
            sources = properties['manufacturing.sourceMembers']
            self.assertEqual(len(sources), 1)
            self.assertEqual(sources[0]['itemKey'], item['key'])
            self.assertEqual(len(sources[0]['spans']), 1)
            span = sources[0]['spans'][0]
            self.assertEqual(span['sourceRange'], [0., 1.])
            self.assertEqual((span['startReserve'], span['endReserve']), (0., 0.))
            self.assertAlmostEqual(math.dist(span['start'], span['end']), expected)

    def test_public_preview_keeps_host_parameters_exact_and_accepts_both_purposes(self):
        values = {**self.defaults, 'clampCount': 0, 'clampTopOffset': 9999.}
        before = deepcopy(values)
        for purpose in ('display', 'manufacturing'):
            result = public_preview('invisible_protective_grille', self.descriptor, values, purpose)
            self.assertEqual(result['schema'], 'icax.'+purpose+'-model')
            self.assertEqual(values, before)

    def test_clear_spacing_and_middle_band_validation_reject_physical_collisions(self):
        invalid = ({'wireCenterSpacing': 2.}, {'wireCenterSpacing': 1.},
                   {'wireCenterSpacing': float('nan')}, {'clampCount': 1.5},
                   {'clampCount': 1, 'clampBottomOffset': 30.},
                   {'clampCount': 1, 'clampBottomOffset': 1170.},
                   {'clampCount': 2, 'clampBottomOffset': 600., 'clampTopOffset': 580.},
                   {'clampCount': 2, 'clampBottomOffset': 700., 'clampTopOffset': 600.},
                   {'height': 100., 'clampWidth': 50., 'clampCount': 0},
                   {'width': 10000., 'wireWidth': 1., 'wireCenterSpacing': 1.01})
        for changes in invalid:
            with self.subTest(changes=changes):
                with self.assertRaises(ValueError):
                    self.state(**changes)
        self.assertEqual(self.state(clampCount=1, clampBottomOffset=30.001)['facts']['middleClampCenters'], [30.001])


if __name__ == '__main__':
    unittest.main()
