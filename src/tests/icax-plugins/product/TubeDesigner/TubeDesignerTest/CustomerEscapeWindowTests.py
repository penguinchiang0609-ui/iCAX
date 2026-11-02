"""Customer V07/V10: shared grid boundaries and nine real opening positions."""
from copy import deepcopy
import unittest
from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition


def opening(design):
    return next(i['properties']['tubeDesigner.escapeWindowLayout'] for i in design['items']
                if 'tubeDesigner.escapeWindowLayout' in i['properties'])


class CustomerEscapeWindowTests(unittest.TestCase):
    def test_nine_positions_share_edges_align_infill_and_preserve_inputs(self):
        _,defaults,template=package('single_face_security_window')
        for x in ('left','center','right'):
            for z in ('bottom','middle','top'):
                with self.subTest(x=x,z=z):
                    p=dict(defaults,doorPosition=x+'_'+z);before=deepcopy(p)
                    design=template.display(p);plan=opening(design)
                    anchor = next(a for a in design['annotations'] if a['parameter'] == 'doorPosition')
                    self.assertEqual(anchor['generatedValue'], p['doorPosition'])
                    self.assertEqual(anchor['kind'], 'parameter')
                    self.assertEqual(p,before)
                    expected={side:side in (x,z) for side in ('left','right','bottom','top')}
                    self.assertEqual(plan['sharedOuterSides'],expected)
                    keys=[i['key'] for i in design['items']]
                    for side,shared in expected.items():
                        self.assertEqual(any(k.startswith('access_door.fixed_frame.'+side+'.') for k in keys),not shared)
                    axes=plan['boundaryAxes']
                    for i in design['items']:
                        member=i['properties']['assemblyFrame.member'];a,b=member['start'],member['end'];key=i['key']
                        if key.startswith('access_door.leaf.horizontal.'):
                            self.assertIn(a[2],plan['rowLines']);self.assertEqual(a[2],b[2])
                        if key.startswith('access_door.leaf.vertical.'):
                            self.assertIn(a[0],plan['columnLines']);self.assertEqual(a[0],b[0])
                        if key.startswith('main_grid.horizontal.') and axes['bottom']<=a[2]<=axes['top']:
                            self.assertTrue(max(a[0],b[0])<=axes['left']+1.e-7 or min(a[0],b[0])>=axes['right']-1.e-7)
                        if key.startswith('main_grid.vertical.') and axes['left']<=a[0]<=axes['right']:
                            self.assertTrue(max(a[2],b[2])<=axes['bottom']+1.e-7 or min(a[2],b[2])>=axes['top']-1.e-7)
                    executed=execute_definition(template.manufacturing(p),design=design)
                    self.assertTrue(executed['items']);self.assertEqual(p,before)
                    for a in design['annotations']:
                        self.assertNotIn(a['parameter'],('doorLeft','doorBottom','doorClearWidth','doorClearHeight'))

    def test_position_is_editable_layout_and_has_an_anchor_on_each_face(self):
        descriptor, defaults, template = package('single_face_security_window')
        dependencies = descriptor['extensions']['parameterDependencies']
        self.assertNotIn('doorPosition', dependencies['creationOnly'])
        self.assertNotIn('doorPosition', dependencies['manufacturingOnly'])
        for face, parameter, surface in [('single', None, None), ('two', 'accessDoorFace2', 'side'),
                ('three', 'accessDoorFace3', 'left'), ('five', 'accessDoorFace5', 'bottom')]:
            with self.subTest(face=face):
                values = dict(defaults, faceType=face, doorPosition='left_top')
                if parameter:
                    values.update({parameter: surface, 'doorClearWidth': 260, 'doorClearHeight': 260,
                                   'depth': 700, 'leftWidth': 700, 'rightWidth': 700})
                design = template.display(values)
                anchors = [a for a in design['annotations'] if a['parameter'] == 'doorPosition']
                self.assertEqual(len(anchors), 1)
                self.assertEqual(anchors[0]['generatedValue'], 'left_top')

    def test_inactive_independent_grid_drafts_do_not_change_the_opening(self):
        _,p,t=package('single_face_security_window');baseline=t.display(p)
        changed=dict(p,doorLeft=99999,doorBottom=99999,doorClearWidth=99999,doorClearHeight=99999,
                     doorVerticalLeftCenterOffset=99999,doorHorizontalTopCenterOffset=99999)
        self.assertEqual(t.display(changed),baseline)
        values=dict(p,accessDoorEnabled=False,doorColumnCount=999,doorRowCount=999,doorPosition='left_bottom')
        self.assertFalse(any(i['key'].startswith('access_door.') for i in t.display(values)['items']))

    def test_invalid_cell_counts_and_leaf_space_are_rejected(self):
        _,p,t=package('single_face_security_window')
        for patch,pattern in [({'doorColumnCount':99},'占格数'),({'doorRowCount':4},'中部'),
                              ({'doorColumnCount':1,'doorGap':100},'容纳')]:
            with self.subTest(patch=patch),self.assertRaisesRegex(ValueError,pattern):t.display(dict(p,**patch))

    def test_partial_frame_does_not_execute_an_inactive_closed_forming_draft(self):
        _,p,t=package('single_face_security_window');p.update(doorPosition='left_bottom',doorFrameJoinType='v_groove_90:tool_library')
        d=t.display(p);executed=execute_definition(t.manufacturing(p),design=d)
        self.assertTrue(executed['items'])
        self.assertFalse(any('fixed_frame.continuous.' in i['key'] for i in executed['items']))


if __name__=='__main__':unittest.main()
