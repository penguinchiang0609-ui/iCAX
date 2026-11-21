"""Actual rect-bar resources use the ordinary stock and machining pipeline."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleSolidBarTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')
        cls.catalog = cls.template._load(cls.template.SHARED/'tube_profile_catalog.py')
        cls.spatial = cls.template._load(cls.template.ROOT/'spatial_manufacturing.py')

    def values(self, **changes):
        return {**self.defaults, 'layoutStyle': 'five_face', 'supportStyle': 'single',
                'width': 1000., 'height': 400., 'projectionDepth': 120., 'topDrop': 30.,
                'frameProfileType': 'rect-bar', 'frameWidth': 10., 'frameDepth': 5.,
                'innerProfileType': 'rect-bar', 'innerWidth': 10., 'innerDepth': 5.,
                'supportProfileType': 'rect-bar', 'supportWidth': 20., 'supportDepth': 10.,
                'projectionVerticalProfileType': 'rect-bar', 'projectionVerticalWidth': 10., 'projectionVerticalDepth': 5.,
                'projectionHorizontalProfileType': 'rect-bar', 'projectionHorizontalWidth': 10., 'projectionHorizontalDepth': 5.,
                'supportMaxSpacing': 600., 'barLayoutMode': 'fixed_count', 'barCount': 3,
                'handleEnabled': 'no', 'installHoleEnabled': 'no',
                'jointMode': 'butt_weld', 'returnJointMode': 'butt_weld', **changes}

    def test_existing_rect_bar_library_returns_actual_solid_section_without_wall(self):
        values = self.values()
        for role, expected in (('frame', (10., 5.)), ('inner', (10., 5.)), ('support', (20., 10.))):
            profile = self.catalog.load_profile(values, role)
            self.assertEqual(profile.kind, 'rect-bar')
            self.assertFalse(profile.hollow)
            self.assertEqual(profile.wall, 0.)
            self.assertEqual((profile.width, profile.depth), expected)
            self.assertEqual(len(profile.contours()), 1)
            self.assertEqual(profile.properties()['hollow'], False)

    def test_planar_bar_display_and_butt_manufacturing_share_real_solid_profiles(self):
        values = self.values(layoutStyle='planar', frameType='closed_frame')
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
            design = self.template.display(values)
            with forbid_processing():
                declaration = self.template.manufacturing(values)
            actual = execute_definition(declaration, design=design)
        self.assertEqual(values, before)
        self.assertEqual(len(actual['items']), len(state['members']))
        for member in state['members']:
            self.assertFalse(member['profile'].hollow)
        for item in design['items']:
            profile = item['properties']['tubeDesigner.profile']
            self.assertEqual(profile['kind'], 'rect-bar')
            self.assertEqual(profile['wallThickness'], 0.)
            self.assertEqual(len(profile['contours']), 1)
        self.assertTrue(all(c['properties']['recipe'] == 'butt_weld' for c in declaration['connections']))

    def test_single_solid_support_stays_whole_and_receives_separate_welded_spans(self):
        values = self.values()
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
            design = self.template.display(values)
            with forbid_processing():
                declaration = self.template.manufacturing(values)
            actual = execute_definition(declaration, design=design)
        members = {m['key']: m for m in state['members']}
        crossings = [c for c in state['connections'] if c.get('kind') == 'cross-through']
        self.assertEqual(crossings, [])
        spans = [m for m in state['members'] if m['role'] == 'inner.full']
        self.assertEqual(len(spans), 6)
        self.assertEqual(len({m['sourceRowKey'] for m in spans}), 3)
        for branch in spans:
            self.assertFalse(branch['profile'].hollow)
            self.assertAlmostEqual(math.dist(branch['start'], branch['end']), 480.)
        self.assertEqual(len(actual['items']), len(state['members']))
        hole_processes = [p for p in declaration['processes'] if p['key'].startswith('frame.support.')
                          and p['key'].endswith('.apertures')]
        self.assertEqual(hole_processes, [])
        self.assertEqual(state['facts']['supportConnectionKind'], 'segmented-weld')
        self.assertFalse(state['facts']['continuousHorizontalRows'])
        self.assertEqual(values, before)
        # Both C20x5 and C20x10 are welded flat stock, never receiving apertures.

    def test_solid_stock_retention_witness_is_real_midpoint_not_a_fabricated_wall(self):
        values = self.values(supportMaxSpacing=2000.)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
        for member in state['members']:
            expected = [(a+b)/2 for a,b in zip(member['start'], member['end'])]
            self.assertLess(math.dist(self.spatial._wall_witness(member), expected), 1.e-9)

    def test_mixed_solid_and_existing_hollow_sections_keep_exact_resource_identity(self):
        values = self.values(frameProfileType='rect', frameWidth=30., frameDepth=20.,
                             frameWallThickness=1., frameCornerRadius=1.,
                             innerProfileType='rect-bar', supportProfileType='rect',
                             supportWidth=20., supportDepth=10., supportWallThickness=1.,
                             supportCornerRadius=1.)
        values.update(projectionVerticalProfileType='rect', projectionVerticalWidth=30., projectionVerticalDepth=20.,
                      projectionHorizontalProfileType='rect', projectionHorizontalWidth=30., projectionHorizontalDepth=20.)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
            declaration = self.template.manufacturing(values)
            actual = execute_definition(declaration, design=self.template.display(values))
        self.assertTrue(all(m['profile'].hollow for m in state['members'] if m['role'].startswith('frame.')))
        self.assertTrue(all(not m['profile'].hollow for m in state['members'] if m['role'].startswith('inner.')))
        self.assertEqual(len(actual['items']), len(state['members']))

    def test_existing_male_head_capability_does_not_silently_fabricate_solid_bar_process(self):
        values = self.values(jointMode='male_female_half_hole', supportMaxSpacing=2000.)
        with _profile_validation_scope(self.descriptor):
            with self.assertRaisesRegex(ValueError, '公母插接'):
                self.template.manufacturing(values)


if __name__ == '__main__':
    unittest.main()
