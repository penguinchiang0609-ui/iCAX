"""F07 is horizontal infill in the existing security-window product."""
from copy import deepcopy
import unittest
from WindowCatalogueTests import package, public_preview
from ProductManufacturingDeclarationTests import execute_definition
from CustomerEscapeWindowTests import opening


def reference(**changes):
    return dict(infillPattern='horizontal',width=600,height=1400,doorClearWidth=200,
        doorClearHeight=240,doorBottom=650,horizontalMaximumCenterSpacing=120,
        horizontalWidth=20,horizontalDepth=20,doorLeafFrameWidth=16,doorLeafFrameDepth=16,
        doorHorizontalWidth=12,doorHorizontalDepth=12,**changes)

class CustomerMinimalGrilleTests(unittest.TestCase):
    def test_same_template_all_positions_and_open_frame(self):
        descriptor,p,t=package('single_face_security_window')
        for side in ('left','center','right'):
            for z in ('bottom','middle','top'):
                for frame in ('four_sides','left_right'):
                    with self.subTest(side=side,z=z,frame=frame):
                        v=dict(p,**reference(doorPosition=side+'_'+z,frameLayout=frame));before=deepcopy(v)
                        d=public_preview('single_face_security_window',descriptor,v,'display');facts=opening(d)
                        self.assertEqual(facts['layout'],'horizontal')
                        self.assertEqual(facts['sharedOuterSides']['left'],side=='left')
                        self.assertEqual(facts['sharedOuterSides']['right'],side=='right')
                        self.assertAlmostEqual(facts['designClearWidth'],200)
                        self.assertAlmostEqual(facts['designClearHeight'],240)
                        self.assertEqual(len([i for i in d['items'] if i['key'].startswith('access_door.leaf.frame.')]),4)
                        self.assertFalse(any(i['key'].startswith('main_grid.vertical.') for i in d['items']))
                        for i in d['items']:
                            if i['key'].startswith('access_door.leaf.horizontal.'):
                                self.assertIn(i['properties']['assemblyFrame.member']['start'][2],facts['rowLines'])
                        result=execute_definition(t.manufacturing(v),design=d)
                        self.assertTrue(result['items']);self.assertEqual(v,before)

    def test_inactive_drafts_and_invalid_openings(self):
        _,p,t=package('single_face_security_window');p.update(reference(doorPosition='right_middle'))
        d=t.display(p)
        self.assertEqual(d,t.display(dict(p,doorColumnCount=99,doorRowCount=99,doorLeft=9999,doorBottom=9999,doorLeftMargin=9999,doorTopMargin=9999)))
        off=dict(p,accessDoorEnabled=False)
        self.assertEqual(t.display(off),t.display(dict(off,doorClearWidth=9999,doorClearHeight=9999,doorBottom=9999)))
        for patch in ({'doorClearWidth':1000},{'doorRightMargin':1},{'doorGap':100}):
            with self.subTest(patch=patch),self.assertRaises(ValueError):t.display(dict(p,**patch))

    def test_position_anchors_and_directional_margins(self):
        _,p,t=package('single_face_security_window')
        for x in ('left','center','right'):
            for z in ('bottom','middle','top'):
                v=dict(p,**reference(doorPosition=x+'_'+z,doorLeftMargin=70,doorRightMargin=80,
                    doorBottomMargin=90,doorTopMargin=100,doorHorizontalOffset=30,doorVerticalOffset=-40))
                plan=opening(t.display(v));bounds=plan['clearBounds'];f=t._profile(v,'frame').width
                if x=='left':self.assertAlmostEqual(bounds['left']-f,70)
                elif x=='right':self.assertAlmostEqual(v['width']-f-bounds['right'],80)
                else:self.assertAlmostEqual((bounds['left']+bounds['right'])/2-v['width']/2,30)
                if z=='bottom':self.assertAlmostEqual(bounds['bottom']-f,90)
                elif z=='top':self.assertAlmostEqual(v['height']-f-bounds['top'],100)
                else:self.assertAlmostEqual((bounds['bottom']+bounds['top'])/2-v['height']/2,-40)

if __name__=='__main__':unittest.main()
