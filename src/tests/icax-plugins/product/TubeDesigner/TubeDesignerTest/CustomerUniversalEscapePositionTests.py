"""All security-window faces and infill patterns use face-local anchors."""
from copy import deepcopy
import unittest
from WindowCatalogueTests import package, public_preview
from CustomerEscapeWindowTests import opening
from ProductManufacturingDeclarationTests import execute_definition


SURFACES=[('single','front','left'),('two','front','left'),('two','front','right'),('two','side','left'),
          ('two','side','right'),('three','left','left'),('three','front','left'),
          ('three','right','left'),('five','left','left'),('five','front','left'),
          ('five','right','left'),('five','bottom','left')]


def parameters(defaults,face,face_name,side,pattern,position,margin=0):
    result=dict(defaults,faceType=face,sidePosition=side,infillPattern=pattern,
        width=1400,height=1800,depth=700,leftWidth=700,rightWidth=700,sideWidth=700,
        doorClearWidth=260,doorClearHeight=260,doorColumnCount=3,doorRowCount=1,
        doorPosition=position,doorLeftMargin=margin,doorRightMargin=margin,
        doorBottomMargin=margin,doorTopMargin=margin,
        doorHorizontalOffset=20 if margin else 0,doorVerticalOffset=-25 if margin else 0)
    if face!='single':result[{'two':'accessDoorFace2','three':'accessDoorFace3','five':'accessDoorFace5'}[face]]=face_name
    return result


class CustomerUniversalEscapePositionTests(unittest.TestCase):
    def test_vertical_leaf_spacing_remains_an_effective_parameter(self):
        _,defaults,t=package('single_face_security_window')
        p=parameters(defaults,'single','front','left','vertical','center_middle')
        p.update(doorClearWidth=600,doorVerticalMaximumCenterSpacing=70)
        dense=t.display(p);sparse=t.display(dict(p,doorVerticalMaximumCenterSpacing=200))
        count=lambda d:sum(i['key'].startswith('access_door.leaf.vertical.') for i in d['items'])
        self.assertGreater(count(dense),count(sparse))
        self.assertIn('doorVerticalMaximumCenterSpacing',[a['parameter'] for a in dense['annotations']])
        self.assertEqual(opening(dense)['clearBounds'],opening(sparse)['clearBounds'])
    def test_partial_frames_and_rectangular_outer_profiles(self):
        _,defaults,t=package('single_face_security_window')
        for frame in ('left_right','top_bottom'):
            for pattern in ('grid','horizontal','vertical'):
                for x in ('left','center','right'):
                    for z in ('bottom','middle','top'):
                        with self.subTest(frame=frame,pattern=pattern,position=x+'_'+z):
                            p=parameters(defaults,'single','front','left',pattern,x+'_'+z)
                            p['frameLayout']=frame;a=opening(t.display(p))
                            for k in ('left','right') if frame=='top_bottom' else ('bottom','top'):
                                self.assertFalse(a['sharedOuterSides'][k])
        for face,name,side in SURFACES[1:]:
            with self.subTest(face=face,surface=name,side=side):
                p=parameters(defaults,face,name,side,'grid','left_top');p.update(frameWidth=38,frameDepth=50)
                d=t.display(p);self.assertTrue(execute_definition(t.manufacturing(p),design=d)['items'])

    def test_every_surface_pattern_and_position(self):
        _,defaults,t=package('single_face_security_window')
        for face,face_name,side in SURFACES:
            for pattern in ('grid','horizontal','vertical'):
                for x in ('left','center','right'):
                    for z in ('bottom','middle','top'):
                        with self.subTest(face=face,surface=face_name,side=side,pattern=pattern,position=x+'_'+z):
                            p=parameters(defaults,face,face_name,side,pattern,x+'_'+z)
                            before=deepcopy(p);d=t.display(p);a=opening(d)
                            self.assertEqual(p,before)
                            expected={k:k in (x,z) for k in ('left','right','bottom','top')}
                            self.assertEqual(a['sharedOuterSides'],expected)
                            keys=[i['key'] for i in d['items']]
                            for k,shared in expected.items():
                                self.assertEqual(any(i.startswith('access_door.fixed_frame.'+k+'.') for i in keys),not shared)
                            self.assertEqual(len([k for k in keys if k.startswith('access_door.leaf.frame.')]),4)
                            self.assertNotIn('doorUOffset',[i['parameter'] for i in d['annotations']])
                            self.assertNotIn('doorLeft',[i['parameter'] for i in d['annotations']])

    def test_margins_offsets_manufacturing_and_inactive_drafts(self):
        _,defaults,t=package('single_face_security_window')
        for face,face_name,side in SURFACES:
            for pattern in ('grid','horizontal','vertical'):
                with self.subTest(face=face,surface=face_name,side=side,pattern=pattern):
                    p=parameters(defaults,face,face_name,side,pattern,'right_top',60)
                    d=t.display(p);a=opening(d);inner=a['positioning']['innerBounds'];clear=a['clearBounds']
                    self.assertAlmostEqual(inner['right']-clear['right'],60)
                    self.assertAlmostEqual(inner['top']-clear['top'],60)
                    changed=dict(p,doorLeft=99999,doorBottom=99999,doorUOffset=99999,doorVOffset=99999,
                                 doorLeftMargin=99999,doorBottomMargin=99999,doorHorizontalOffset=99999,doorVerticalOffset=99999)
                    self.assertEqual(t.display(changed),d)
                    self.assertTrue(execute_definition(t.manufacturing(p),design=d)['items'])
                    p=parameters(defaults,face,face_name,side,pattern,'center_middle',60)
                    a=opening(t.display(p));b=a['clearBounds'];inner=a['positioning']['innerBounds']
                    self.assertAlmostEqual((b['left']+b['right']-inner['left']-inner['right'])/2,20)
                    self.assertAlmostEqual((b['bottom']+b['top']-inner['bottom']-inner['top'])/2,-25)
                    with self.assertRaises(ValueError):t.display(dict(p,doorHorizontalOffset=99999))


if __name__=='__main__':unittest.main()
