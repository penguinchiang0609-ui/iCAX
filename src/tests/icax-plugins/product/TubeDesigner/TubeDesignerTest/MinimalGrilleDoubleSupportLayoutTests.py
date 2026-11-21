"""Paired flat-bar supports produce real spans and an empty middle opening."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleDoubleSupportLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')

    def state(self, **changes):
        p = {**self.defaults, 'layoutStyle':'balcony_closure', 'width':1800., 'height':600.,
             'handleEnabled':'no', 'barLayoutMode':'fixed_count', 'barCount':3, **changes}
        before = deepcopy(p)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(p)
        self.assertEqual(p, before)
        return p, state

    def test_new_front_and_infill_defaults_are_actual_solid_flat_bars(self):
        p,s = self.state()
        self.assertEqual(p['supportStyle'], 'double')
        for role in ('frame','inner','support'):
            self.assertEqual(p[role+'ProfileType'], 'rect-bar')
        for m in s['members']:
            if m.get('profileRole') in ('projectionVertical','projectionHorizontal'):
                continue
            self.assertFalse(m['profile'].hollow)
            self.assertEqual(len(m['profile'].contours()),1)

    def test_each_row_ends_on_the_outer_sides_of_pairs_and_leaves_the_gap_empty(self):
        for style in ('five_face','balcony_closure'):
            with self.subTest(style=style):
                p,s = self.state(layoutStyle=style)
                groups = s['facts']['supportGroupCenters']
                posts = [m for m in s['members'] if m['role']=='frame.support']
                rows = [m for m in s['members'] if m['role']=='inner.full']
                self.assertEqual(len(posts),2*len(groups))
                self.assertEqual(len(rows),3*(len(groups)+1))
                self.assertFalse(any(c.get('kind')=='cross-through' for c in s['connections']))
                for z in {m['start'][2] for m in rows}:
                    row = sorted([m for m in rows if m['start'][2]==z],key=lambda m:m['start'][0])
                    for i,(left,right) in enumerate(zip(posts[::2],posts[1::2])):
                        self.assertEqual(row[i]['end'][0],left['start'][0]-10)
                        self.assertEqual(row[i+1]['start'][0],right['start'][0]+10)
                        self.assertAlmostEqual(right['start'][0]-left['start'][0]-20,p['supportPairClearGap'])
                        self.assertFalse(any(m['start'][0] < groups[i] < m['end'][0] for m in row))
                for key in ('frame.top.0001','frame.bottom.0001'):
                    a = next(m for m in s['members'] if m['key']==key)
                    self.assertEqual((a['start'][0],a['end'][0]),(10,1790))
                by_key = {m['key']:m for m in s['members']}
                geometry = self.template._load(self.template.ROOT/'spatial_geometry.py')
                for c in s['connections']:
                    self.assertIn(c['branchKey'],by_key)
                    roots = geometry.ray_intersections(by_key[c['hostKey']],c['point'],c['normal'])
                    self.assertAlmostEqual(roots[1],0,places=6)

    def test_clear_gap_changes_stock_lengths_and_dimension_uses_inner_surfaces(self):
        p,a = self.state(supportPairClearGap=20)
        q,b = self.state(supportPairClearGap=60)
        def lengths(s):
            return sum(math.dist(m['start'],m['end']) for m in s['members'] if m['role']=='inner.full')
        self.assertAlmostEqual(lengths(a)-lengths(b),3*len(a['facts']['supportGroupCenters'])*40)
        with _profile_validation_scope(self.descriptor):
            display = self.template.display(q)
        d = next(d for d in display['annotations'] if d['parameter']=='supportPairClearGap')
        self.assertEqual(d['end'][0]-d['start'][0],60)
        self.assertEqual(d['generatedValue'],60)

    def test_narrow_grille_has_one_pair_without_false_twice_group_width_limit(self):
        p,s = self.state(width=100, supportMaxSpacing=50)
        self.assertEqual(s['facts']['supportGroupCenters'],[50])
        self.assertEqual(s['facts']['supportPositions'],[30,70])
        self.assertEqual(len([m for m in s['members'] if m['role']=='inner.full']),6)

    def test_groups_clear_all_handle_openings_and_respect_maximum_group_spacing(self):
        for side in ('left','right','center_double'):
            p,s = self.state(width=1800,height=1250,handleEnabled='yes',handleSide=side,supportMaxSpacing=600)
            centers = [5,*s['facts']['supportGroupCenters'],1795]
            self.assertLessEqual(max(b-a for a,b in zip(centers,centers[1:])),600+1.e-7)
            for post in (m for m in s['members'] if m['role']=='frame.support'):
                x = post['start'][0]
                for opening in s['facts']['clearOpenings']:
                    self.assertTrue(x+10<=opening['left'] or x-10>=opening['right'])

    def test_infeasible_handle_spacing_is_rejected_instead_of_generating_sliver_stocks(self):
        with self.assertRaisesRegex(ValueError,'最大中心距冲突'):
            self.state(width=1800,height=1250,handleEnabled='yes',handleSide='center_double',supportMaxSpacing=500)

    def test_actual_default_spatial_inputs_generate_and_execute_without_mutation(self):
        for style in ('five_face','balcony_closure'):
            p = {**self.defaults,'layoutStyle':style}
            before = deepcopy(p)
            with _profile_validation_scope(self.descriptor):
                state = self.template.layout(p)
                actual = execute_definition(self.template.manufacturing(p),design=self.template.display(p))
            self.assertEqual(len(actual['items']),len(state['members']))
            self.assertEqual(p,before)

    def test_short_links_are_separate_stocks_at_independent_heights(self):
        for style in ('five_face','balcony_closure'):
            p,base = self.state(layoutStyle=style)
            p,s = self.state(layoutStyle=style,supportLinkCount=3,
                             supportLinkBottomOffset=70,supportLinkTopOffset=90)
            self.assertEqual(s['facts']['supportLinkCenters'],[70,290,510])
            links = [m for m in s['members'] if m['role']=='inner.support_link']
            self.assertEqual(len(links),3*len(s['facts']['supportGroupCenters']))
            def existing(state):
                return [(m['key'],m['start'],m['end']) for m in state['members'] if m['role']!='inner.support_link']
            self.assertEqual(existing(base),existing(s))
            posts = [m for m in s['members'] if m['role']=='frame.support']
            for group,(left,right) in enumerate(zip(posts[::2],posts[1::2]),1):
                for index,z in enumerate([70,290,510],1):
                    link = next(m for m in links if m['key']==f'inner.support_link.{group:04d}.{index:04d}')
                    self.assertEqual(link['start'],(left['start'][0]+10,0,z))
                    self.assertEqual(link['end'],(right['start'][0]-10,0,z))
                    contacts = [c for c in s['connections'] if c['branchKey']==link['key']]
                    self.assertEqual({c['hostKey'] for c in contacts},{left['key'],right['key']})
                    self.assertTrue(all(c.get('kind')!='cross-through' for c in contacts))
            with _profile_validation_scope(self.descriptor):
                actual = execute_definition(self.template.manufacturing(p),design=self.template.display(p))
                annotations = {d['parameter']:d for d in self.template.display(p)['annotations']}
            self.assertEqual(len(actual['items']),len(s['members']))
            self.assertEqual(annotations['supportLinkCount']['generatedValue'],3)
            self.assertEqual(annotations['supportLinkBottomOffset']['end'][2],70)
            self.assertEqual(annotations['supportLinkTopOffset']['end'][2],510)
            self.assertEqual(annotations['supportLinkTopOffset']['start'][2],600)

    def test_one_short_link_ignores_top_and_zero_ignores_both_positions(self):
        p,s = self.state(supportLinkCount=1,supportLinkBottomOffset=135,supportLinkTopOffset=-999)
        self.assertEqual(s['facts']['supportLinkCenters'],[135])
        with _profile_validation_scope(self.descriptor):
            keys = {d['parameter'] for d in self.template.display(p)['annotations']}
        self.assertIn('supportLinkBottomOffset',keys)
        self.assertNotIn('supportLinkTopOffset',keys)
        _,s = self.state(supportLinkCount=0,supportLinkBottomOffset=-999,supportLinkTopOffset=-999)
        self.assertEqual(s['facts']['supportLinkCenters'],[])
        self.assertFalse(any(m['role']=='inner.support_link' for m in s['members']))

    def test_short_links_cannot_overlap_frames_or_each_other(self):
        for changes in ({'supportLinkCount':1,'supportLinkBottomOffset':5},
                        {'supportLinkCount':3,'supportLinkBottomOffset':290,'supportLinkTopOffset':290},
                        {'supportLinkCount':2.5}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                self.state(**changes)


if __name__ == '__main__':
    unittest.main()
