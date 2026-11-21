"""Geometry/stock invariants for five-face grilles; native acceptance is separate."""
from copy import deepcopy
import math
import unittest

from MinimalProtectiveGrilleManufacturingTests import package


class MinimalGrilleFiveFaceLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')

    def values(self, **changes):
        values = {**self.defaults, 'layoutStyle':'five_face', 'supportStyle':'single',
                'frameProfileType':'rect', 'innerProfileType':'rect', 'supportProfileType':'rect',
                'supportWidth':20, 'supportDepth':10, **changes}
        for prefix in ('projectionVertical', 'projectionHorizontal'):
            for suffix in ('ProfileType', 'Width', 'Depth', 'WallThickness', 'CornerRadius'):
                if prefix+suffix not in changes:
                    values[prefix+suffix] = values['frame'+suffix]
        return values

    def layout(self, p):
        before = deepcopy(p)
        result = self.template.layout(p)
        self.assertEqual(p, before)
        self.assertEqual(len(result['members']), len({m['key'] for m in result['members']}))
        return result

    def test_default_roof_drains_to_front_and_bottom_remains_horizontal(self):
        p = self.values()
        state = self.layout(p)
        self.assertEqual(state['facts']['topDrop'], 30)
        top = [m for m in state['members'] if m['face'] == 'top']
        bottom = [m for m in state['members'] if m['face'] == 'bottom']
        self.assertTrue(top and bottom)
        yspan = state['facts']['mountingPlaneY']
        for m in top:
            delta = [b-a for a,b in zip(m['start'], m['end'])]
            self.assertAlmostEqual(delta[2]/delta[1], 30/yspan)
        for m in bottom:
            self.assertAlmostEqual(m['start'][2], m['end'][2])
        for m in state['members']:
            frame = m['sectionFrame']
            u,v,w = [frame[k] for k in ('xAxis','yAxis','zAxis')]
            self.assertAlmostEqual(sum(a*b for a,b in zip(u,v)), 0)
            cross = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
            self.assertLess(math.dist(cross,w), 1.e-8)
            self.assertLess(math.dist(frame['originAtStart'],m['start']), 1.e-8)

    def test_support_density_never_splits_or_shortens_horizontal_stocks(self):
        p = self.values(width=2400, handleEnabled='no', supportMaxSpacing=800)
        sparse = self.layout(p)
        dense = self.layout({**p, 'supportMaxSpacing':500})
        def rails(state):
            return [(m['key'],m['start'],m['end']) for m in state['members']
                    if m['face']=='front' and m['role'].startswith('inner.')]
        self.assertEqual(rails(sparse),rails(dense))
        self.assertGreater(len(dense['facts']['supportPositions']), len(sparse['facts']['supportPositions']))
        for state,maximum in ((sparse,800),(dense,500)):
            frame = next(m['profile'] for m in state['members'] if m['role']=='frame.left')
            columns = [frame.width/2,*state['facts']['supportPositions'],p['width']-frame.width/2]
            self.assertLessEqual(max(b-a for a,b in zip(columns,columns[1:])),maximum+1.e-7)
            members = {m['key']:m for m in state['members']}
            crosses = [c for c in state['connections'] if c.get('kind')=='cross-through']
            self.assertTrue(crosses)
            for c in crosses:
                self.assertEqual(members[c['hostKey']]['profile'].width,20)
                self.assertEqual(members[c['hostKey']]['profile'].depth,10)
                self.assertEqual(members[c['branchKey']]['profile'].width,10)
                self.assertNotIn('branchEnd',c)

    def test_support_columns_leave_handle_openings_and_jambs_clear(self):
        for side in ('left','right','center_double'):
            state = self.layout(self.values(width=1800,handleSide=side,supportMaxSpacing=500))
            for post in (m for m in state['members'] if m['role']=='frame.support'):
                x,half = post['start'][0],post['profile'].width/2
                for opening in state['facts']['clearOpenings']:
                    self.assertTrue(x+half <= opening['left'] or x-half >= opening['right'])

    def test_double_handle_middle_divider_does_not_cut_other_rows(self):
        state = self.layout(self.values(width=1800,handleSide='center_double'))
        center = [m for m in state['members'] if m['role']=='inner.center']
        self.assertEqual(len(center),1)
        opening = state['facts']['clearOpenings'][0]
        self.assertEqual(center[0]['start'][2],opening['bottom'])
        self.assertEqual(center[0]['end'][2],opening['top'])
        for role in ('inner.upper','inner.lower','inner.upper_boundary','inner.lower_boundary'):
            rows = [m for m in state['members'] if m['role']==role]
            self.assertEqual(len(rows),len({m['start'][2] for m in rows}))

    def test_inactive_planar_drafts_do_not_change_five_face_geometry(self):
        a = self.layout(self.values(frameType='two_vertical',firstBarTopOffset=0,lastBarBottomOffset=0))
        b = self.layout(self.values(frameType='bottom_rail',firstBarTopOffset=9999,lastBarBottomOffset=9999))
        self.assertEqual([(m['key'],m['start'],m['end']) for m in a['members']],
                         [(m['key'],m['start'],m['end']) for m in b['members']])

    def test_inactive_spatial_drafts_do_not_change_planar_geometry(self):
        a = self.layout({**self.defaults,'layoutStyle':'planar'})
        b = self.layout({**self.defaults,'layoutStyle':'planar','projectionDepth':1,'topDrop':1000,
                         'supportMaxSpacing':1,'returnMaximumClearGap':1})
        self.assertEqual([(m['key'],m['start'],m['end']) for m in a['members']],
                         [(m['key'],m['start'],m['end']) for m in b['members']])

    def test_frame_outside_depth_and_rear_installation_offsets_are_real(self):
        p = self.values(frameWidth=30,frameDepth=20,installHoleEnabled='yes',height=800)
        state = self.layout(p)
        rear = next(m for m in state['members'] if m['role']=='frame.left')
        self.assertAlmostEqual(rear['start'][1]+rear['profile'].depth,p['projectionDepth'])
        plan = state['facts']['installationHoles']
        self.assertAlmostEqual(plan['centers'][-1],830-p['installHoleTopOffset'])
        for hole in plan['groups']:
            self.assertAlmostEqual(hole['point'][1],rear['start'][1])

    def test_all_endpoint_joints_use_true_surface_points(self):
        p = self.values(frameWidth=30,frameDepth=20,frameCornerRadius=3,projectionDepth=150)
        state = self.layout(p)
        members = {m['key']:m for m in state['members']}
        geometry = self.template._load(self.template.ROOT/'spatial_geometry.py')
        for c in state['connections']:
            if c.get('kind')=='cross-through':
                continue
            roots = geometry.ray_intersections(members[c['hostKey']],c['point'],c['normal'])
            self.assertAlmostEqual(roots[1],0,places=6)
            self.assertLess(roots[0],0)

    def test_empty_front_rows_still_build_the_actual_return_faces(self):
        state = self.layout(self.values(handleEnabled='no',barLayoutMode='fixed_count',barCount=0))
        self.assertFalse(any(m['role']=='inner.full' for m in state['members']))
        self.assertTrue(any(m['face']=='top' for m in state['members']))
        self.assertTrue(any(m['face']=='bottom' for m in state['members']))

    def test_empty_front_rows_do_not_allow_overlapping_boundary_returns(self):
        # No middle return is needed at this gap. That empty list must not
        # bypass checking the two actual boundary stocks against each other.
        for depth in (90, 200):
            with self.subTest(projectionHorizontalDepth=depth):
                p = self.values(width=100, handleEnabled='no', barLayoutMode='fixed_count',
                                barCount=0, projectionHorizontalDepth=depth,
                                projectionDepth=1000)
                before = deepcopy(p)
                with self.assertRaisesRegex(ValueError, '左右边界回杆之间须保留有效净空'):
                    self.template.layout(p)
                self.assertEqual(p, before)

    def test_roof_and_bottom_returns_clear_the_wider_support_shoulders(self):
        for width in (1200,1800,2400):
            state = self.layout(self.values(width=width,handleEnabled='no'))
            supports = [m for m in state['members'] if m['role']=='frame.support']
            for rod in (m for m in state['members'] if m['face'] in ('top','bottom')):
                self.assertGreater(rod['start'][0]-rod['profile'].depth/2,self.defaults['frameWidth'])
                self.assertLess(rod['start'][0]+rod['profile'].depth/2,width-self.defaults['frameWidth'])
                for post in supports:
                    clearance = abs(rod['start'][0]-post['start'][0])-(post['profile'].width+rod['profile'].depth)/2
                    self.assertGreaterEqual(clearance, -1.e-7)
            self.assertLessEqual(max(state['facts']['returnClearGaps']),self.defaults['returnMaximumClearGap']+1.e-7)


if __name__ == '__main__':
    unittest.main()
