"""Flat supports stay whole; real B spans weld to their exterior surfaces."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleFlatSupportWeldTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')
        cls.geometry = cls.template._load(cls.template.ROOT/'spatial_geometry.py')

    def state(self, **changes):
        values = {**self.defaults, 'layoutStyle':'five_face', 'width':1000., 'height':400.,
                  'handleEnabled':'no', 'barLayoutMode':'fixed_count', 'barCount':3,
                  'supportStyle':'single', 'supportMaxSpacing':600., **changes}
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
        self.assertEqual(values, before)
        return values, state

    def execute(self, values, state):
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            design = self.template.display(values)
            with forbid_processing():
                declaration = self.template.manufacturing(values)
            actual = execute_definition(declaration, design=design)
        self.assertEqual(values, before)
        self.assertEqual(len(actual['items']), len(state['members']))
        return declaration

    def test_both_thin_and_thick_single_flat_supports_are_welded_without_apertures(self):
        for style in ('five_face', 'balcony_closure'):
            for depth in (5., 10.):
                with self.subTest(style=style, supportDepth=depth):
                    values, state = self.state(layoutStyle=style, supportDepth=depth)
                    posts = [m for m in state['members'] if m['role']=='frame.support']
                    rows = [m for m in state['members'] if m['role']=='inner.full']
                    self.assertEqual(len(posts), 1)
                    self.assertEqual(posts[0]['start'], (500.,0.,10.))
                    self.assertEqual(posts[0]['end'], (500.,0.,390.))
                    self.assertEqual(len(rows), 6)
                    for z in sorted({m['start'][2] for m in rows}):
                        pair = sorted((m for m in rows if m['start'][2]==z), key=lambda m:m['start'][0])
                        self.assertEqual([(m['start'][0],m['end'][0]) for m in pair], [(10.,490.),(510.,990.)])
                        self.assertEqual(pair[0]['sourceRowKey'], pair[1]['sourceRowKey'])
                    self.assertFalse(any(c.get('kind')=='cross-through' for c in state['connections']))
                    self.assertEqual(state['facts']['supportConnectionKind'], 'segmented-weld')
                    self.assertFalse(state['facts']['continuousHorizontalRows'])
                    declaration = self.execute(values, state)
                    self.assertFalse(any(p['key'].startswith('frame.support.') and p['key'].endswith('.apertures')
                                         for p in declaration['processes']))
                    joints = [c for c in declaration['connections'] if c['items'][0]==posts[0]['key']]
                    self.assertEqual(len(joints), 6)
                    self.assertTrue(all(c['kind']=='weld' and c['properties']['recipe']=='butt_weld' for c in joints))

    def test_hollow_single_support_preserves_existing_through_rows(self):
        values, state = self.state(supportProfileType='rect', supportDepth=10.)
        rows = [m for m in state['members'] if m['role']=='inner.full']
        self.assertEqual(len(rows), 3)
        self.assertTrue(all(m['start'][0]==10. and m['end'][0]==990. for m in rows))
        self.assertEqual(len([c for c in state['connections'] if c.get('kind')=='cross-through']), 3)
        self.assertEqual(state['facts']['supportConnectionKind'], 'through')
        self.assertTrue(state['facts']['continuousHorizontalRows'])
        declaration = self.execute(values, state)
        self.assertTrue(any(p['key']=='frame.support.0001.apertures' for p in declaration['processes']))

    def test_no_real_support_or_no_front_rows_does_not_claim_a_stock_split(self):
        for changes in ({'supportMaxSpacing':2000.}, {'barCount':0}):
            with self.subTest(changes=changes):
                _, state = self.state(**changes)
                self.assertTrue(state['facts']['continuousHorizontalRows'])
                self.assertFalse(any('sourceRowKey' in m for m in state['members']))
                self.assertFalse(any(c.get('kind')=='cross-through' for c in state['connections']))

    def test_front_frame_and_support_stocks_are_not_split_by_the_new_welds(self):
        _, baseline = self.state(supportProfileType='rect', supportDepth=10.)
        _, welded = self.state(supportProfileType='rect-bar', supportDepth=10.)
        signature = lambda s: [(m['key'],m['start'],m['end']) for m in s['members']
                               if m['face']=='front' and m['role'].startswith('frame.')]
        self.assertEqual(signature(baseline), signature(welded))

    def test_handles_and_multiple_posts_have_real_endpoint_contacts_without_old_row_keys(self):
        for side in ('left','right','center_double'):
            with self.subTest(side=side):
                _, state = self.state(width=1800.,height=1250.,handleEnabled='yes',handleSide=side)
                members = {m['key']:m for m in state['members']}
                self.assertGreater(len([m for m in members.values() if m['role']=='frame.support']), 1)
                for c in state['connections']:
                    self.assertIn(c['hostKey'], members)
                    self.assertIn(c['branchKey'], members)
                    self.assertNotEqual(c.get('kind'), 'cross-through')
                    roots = self.geometry.ray_intersections(members[c['hostKey']],c['point'],c['normal'])
                    self.assertAlmostEqual(roots[1],0.,places=6)
                    self.assertLess(math.dist(c['point'],members[c['branchKey']][c['branchEnd']]),1.e-7)

    def test_global_male_setting_cannot_drill_a_flat_support(self):
        values, state = self.state(frameProfileType='rect',frameWidth=40.,frameDepth=30.,frameWallThickness=1.5,
                                  innerProfileType='rect',jointMode='male_female_half_hole',maleHeadWidth=4.,
                                  maleHeadLength=3.,minimumFeatureWeb=.2)
        declaration = self.execute(values, state)
        support_joints = [c for c in declaration['connections'] if c['items'][0].startswith('frame.support.')]
        self.assertTrue(support_joints)
        self.assertTrue(all(c['properties']['recipe']=='butt_weld' for c in support_joints))
        self.assertFalse(any(p['key'].startswith('frame.support.') and p['key'].endswith('.apertures')
                             for p in declaration['processes']))
        self.assertTrue(any(c['properties']['recipe']=='male_female_half_hole' for c in declaration['connections']))

    def test_double_support_short_links_remain_independent_whole_stocks(self):
        _, state = self.state(supportStyle='double',supportLinkCount=2,
                              supportLinkBottomOffset=100.,supportLinkTopOffset=100.)
        posts = [m for m in state['members'] if m['role']=='frame.support']
        links = [m for m in state['members'] if m['role']=='inner.support_link']
        self.assertEqual(len(posts),2)
        self.assertEqual(len(links),2)
        self.assertEqual([(m['start'][0],m['end'][0]) for m in links],[(490.,510.),(490.,510.)])
        self.assertTrue(all('sourceRowKey' not in m for m in links))
        self.assertFalse(any(c.get('kind')=='cross-through' for c in state['connections']))


if __name__ == '__main__':
    unittest.main()
