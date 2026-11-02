"""Customer maximum pitch: auto counts, boundaries and retained offset drafts."""
from copy import deepcopy
import unittest
from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition


class CustomerAutomaticGridTests(unittest.TestCase):
    def test_actual_axes_are_equal_include_edge_intervals_and_ignore_offset_drafts(self):
        _,p,t=package('single_face_security_window')
        for width,height,spacing in [(1250,1850,120),(1200,1800,115),(200,300,500),(1235,1777,97)]:
            values=dict(p,width=width,height=height,verticalMaximumCenterSpacing=spacing,
                        horizontalMaximumCenterSpacing=spacing,accessDoorEnabled=False)
            before=deepcopy(values);d=t.display(values)
            axes={'horizontal':set(),'vertical':set()}
            for item in d['items']:
                key=item['key'];member=item['properties']['assemblyFrame.member'];start=member['start']
                if key.startswith('main_grid.horizontal.'):axes['horizontal'].add(start[2])
                if key.startswith('main_grid.vertical.'):axes['vertical'].add(start[0])
            frame=t._profile(values,'frame').width
            for direction,length in [('horizontal',height),('vertical',width)]:
                coords=[frame,*sorted(axes[direction]),length-frame]
                gaps=[b-a for a,b in zip(coords,coords[1:])]
                self.assertTrue(gaps);self.assertLessEqual(max(gaps),spacing+1.e-7)
                self.assertLess(max(gaps)-min(gaps),1.e-7)
            changed=dict(values,firstHorizontalTopOffset=9999,lastHorizontalBottomOffset=9999,
                         verticalLeftCenterOffset=9999,verticalRightCenterOffset=9999)
            other=t.display(changed)
            self.assertEqual(other['items'],d['items'])
            self.assertEqual(values,before)

    def test_manual_offsets_remain_explicit_and_multi_faces_generate(self):
        _,p,t=package('single_face_security_window')
        for face in ('single','two','three','five'):
            for mode in ('automatic','end_offsets'):
                with self.subTest(face=face,mode=mode):
                    v=dict(p,faceType=face,barDistribution=mode,accessDoorEnabled=False)
                    self.assertTrue(t.display(v)['items'])

    def test_five_face_aligned_insertion_stops_before_the_other_branch(self):
        _,p,t=package('single_face_security_window');p.update(faceType='five',accessDoorEnabled=False)
        result=execute_definition(t.manufacturing(p),design=t.display(p))
        calls={c['instanceId']:c for c in result['extensions']['tubeDesigner.assemblyProcessSource']['instances']}
        for key in ('main_grid.vertical.0005.end.end','cap_grid.vertical.0001.end.start'):
            self.assertAlmostEqual(calls[key]['parameters']['insertionDepth'],9.3)
        self.assertEqual(p['verticalBranchReserve'],10)


if __name__=='__main__':unittest.main()
