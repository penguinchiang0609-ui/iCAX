"""Independent projection frame materials preserve the front and outside datums."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleMixedProjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')

    def state(self, **changes):
        p = {**self.defaults, 'layoutStyle':'five_face', 'width':1800., 'height':600.,
             'handleEnabled':'no', 'barLayoutMode':'fixed_count', 'barCount':3, **changes}
        before = deepcopy(p)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(p)
        self.assertEqual(p,before)
        return p,state

    def test_projection_materials_are_independent_real_sections(self):
        for style in ('five_face','balcony_closure'):
            p,s = self.state(layoutStyle=style,supportLinkCount=3)
            vertical = [m for m in s['members'] if m.get('profileRole')=='projectionVertical']
            horizontal = [m for m in s['members'] if m.get('profileRole')=='projectionHorizontal']
            self.assertEqual(len(vertical),2 if style=='five_face' else 0)
            self.assertEqual(len(horizontal),6)
            for group,dimension in ((vertical,15),(horizontal,12)):
                for m in group:
                    profile=m['profile']
                    self.assertEqual(profile.kind,'rect')
                    self.assertTrue(profile.hollow)
                    self.assertEqual((profile.width,profile.depth),(dimension,dimension))
                    self.assertEqual(len(profile.contours()),2)
            for m in s['members']:
                if m not in vertical+horizontal:
                    self.assertEqual(m['profile'].kind,'rect-bar')
                    self.assertFalse(m['profile'].hollow)
            self.assertEqual({m['key'] for m in horizontal},
                             {'frame.rear_top.0001','frame.rear_bottom.0001',
                              'frame.return_left.bottom','frame.return_left.top',
                              'frame.return_right.bottom','frame.return_right.top'})

    def test_projection_material_edit_does_not_shift_front_or_recut_ordinary_rails(self):
        _,a=self.state(supportLinkCount=3)
        _,b=self.state(supportLinkCount=3,projectionVerticalWidth=18,projectionVerticalDepth=18,
                      projectionHorizontalWidth=14,projectionHorizontalDepth=14)
        def front(s):
            return [(m['key'],m['start'],m['end'],m['profile'].kind,m['profile'].width,m['profile'].depth)
                    for m in s['members'] if m.get('face')=='front']
        self.assertEqual(front(a),front(b))
        self.assertEqual(len(a['members']),len(b['members']))

    def test_progressive_depths_and_annotations_measure_outer_faces(self):
        for style in ('five_face','balcony_closure'):
            p,s=self.state(layoutStyle=style,projectionDepth=150,balconyTopDepth=160,
                           balconyBottomDepth=240,balconyTopDrop=30,
                           projectionVerticalDepth=18,projectionHorizontalDepth=14)
            f=s['facts']
            self.assertEqual(f['frontOuterY'],-2.5)
            self.assertEqual(f['topRearOuterY']-f['frontOuterY'],150 if style=='five_face' else 160)
            self.assertEqual(f['bottomRearOuterY']-f['frontOuterY'],150 if style=='five_face' else 240)
            members={m['key']:m for m in s['members']}
            for key,outer in (('frame.rear_top.0001',f['topRearOuterY']),
                              ('frame.rear_bottom.0001',f['bottomRearOuterY'])):
                m=members[key]
                self.assertAlmostEqual(m['start'][1]+m['profile'].depth/2,outer)
            for m in s['members']:
                if m['face']=='bottom' or m['key'].endswith('.bottom'):
                    self.assertAlmostEqual(m['start'][2],m['end'][2])
            with _profile_validation_scope(self.descriptor):
                annotations={d['parameter']:d for d in self.template.display(p)['annotations']}
            keys=['projectionDepth'] if style=='five_face' else ['balconyTopDepth','balconyBottomDepth']
            for key in keys:
                d=annotations[key]
                self.assertAlmostEqual(d['end'][1]-d['start'][1],p[key])

    def test_each_joint_and_section_frame_uses_actual_geometry(self):
        p,s=self.state(supportLinkCount=3)
        g=self.template._load(self.template.ROOT/'spatial_geometry.py')
        members={m['key']:m for m in s['members']}
        off_axis=False
        for m in s['members']:
            g.axes(m)
        for c in s['connections']:
            if c.get('kind')=='cross-through':
                continue
            host=members[c['hostKey']]
            roots=g.ray_intersections(host,c['point'],c['normal'])
            self.assertAlmostEqual(roots[1],0,places=6)
            self.assertLess(roots[0],0)
            branch=members[c['branchKey']]
            self.assertLess(math.dist(c['point'],branch[c['branchEnd']]),1.e-6)
            if c['branchKey'].startswith('frame.return_'):
                off_axis=True
        self.assertTrue(off_axis)

    def test_display_categories_and_shared_manufacturing_use_material_roles(self):
        p,s=self.state(supportLinkCount=3)
        before=deepcopy(p)
        with _profile_validation_scope(self.descriptor):
            design=self.template.display(p)
            actual=execute_definition(self.template.manufacturing(p),design=design)
        self.assertEqual(p,before)
        self.assertEqual(len(actual['items']),len(s['members']))
        by_key={item['key']:item for item in design['items']}
        for m in s['members']:
            role=m.get('profileRole')
            if role in ('projectionVertical','projectionHorizontal'):
                self.assertEqual(by_key[m['key']]['properties']['categoryKey'],'minimal-grille.'+role)

    def test_inactive_material_drafts_do_not_affect_planar_or_add_balcony_uprights(self):
        for style in ('planar','balcony_closure'):
            _,a=self.state(layoutStyle=style)
            changes={'projectionVerticalProfileType':'unsupported','projectionVerticalWidth':-1}
            if style=='planar':
                changes.update(projectionHorizontalProfileType='unsupported',projectionHorizontalWidth=-1)
            _,b=self.state(layoutStyle=style,**changes)
            self.assertEqual([(m['key'],m['start'],m['end']) for m in a['members']],
                             [(m['key'],m['start'],m['end']) for m in b['members']])
            self.assertFalse(any(m['key'].startswith('frame.rear_left.') for m in b['members']))


if __name__=='__main__':
    unittest.main()
