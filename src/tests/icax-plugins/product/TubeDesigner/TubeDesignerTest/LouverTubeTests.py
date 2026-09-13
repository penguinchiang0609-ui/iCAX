"""Geometry/contract tests for fixed tube louvers, independent of native BRep tests."""
from copy import deepcopy
import math
import unittest
from WindowCatalogueTests import package


class LouverTubeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.module = package('louver_window')

    def values(self, **changes):
        return dict(self.defaults, **changes)

    def test_direction_roll_array_connections_and_contract(self):
        for angle in (0,90,30,-45,179):
            for roll in (0,30,90,-45):
                for connection in ('face_weld','slot_insert','through_insert'):
                    for mode in ('pitch','gap','overlap','count'):
                        # Overlap at 0/90 cannot be physically achieved; cover rejection separately.
                        if mode=='overlap' and roll in (0,90):
                            continue
                        with self.subTest(angle=angle,roll=roll,connection=connection,mode=mode):
                            p=self.values(width=600,height=600,bladeDirectionAngle=angle,bladeRollAngle=roll,
                                          bladeConnection=connection,arrayMode=mode,bladePitch=100,bladeCount=3)
                            before=deepcopy(p)
                            for purpose in ('display','manufacturing'):
                                result=self.module.generate(p,{'template':self.descriptor,'geometryPurpose':purpose})
                                self.assertEqual(result['parameters'],before)
                                self.assertEqual(p,before)
                                self.assertGreater(len(result['items']),4)
                                self.assertTrue(all(i['properties']['manufacturing.partKind']=='tube' for i in result['items']))
                                self.assertEqual(sum(len(g) for g in result['extensions']['louver']['partGroups']),len(result['items']))

    def test_exact_projection_and_array_formula(self):
        for roll in (0,30,45,90):
            p=self.values(bladeRollAngle=roll,arrayMode='count',bladeCount=5,startOffset=10,endOffset=20)
            state=self.module.layout(p)
            a=math.radians(roll)
            expected=76*abs(math.sin(a))+16*abs(math.cos(a))+4
            self.assertAlmostEqual(state['hp'],expected)
            self.assertAlmostEqual(state['arrays'][0]['pitch'],(1500-80-30-expected)/4)
        single=self.module.layout(self.values(arrayMode='count',bladeCount=1))
        self.assertEqual(len(single['blades']),1)
        self.assertAlmostEqual(single['blades'][0]['position'],750)
        # Rounded corners permit this clearance although bounding rectangles overlap.
        state=self.module.layout(self.values(bladeRollAngle=math.degrees(math.atan(4)),bladePitch=82))
        self.assertGreater(len(state['blades']),1)

    def test_end_lengths_planes_and_real_slot_boolean(self):
        for connection,extra in [('face_weld',0),('slot_insert',20),('through_insert',80)]:
            p=self.values(bladeConnection=connection)
            state=self.module.layout(p)
            self.assertAlmostEqual(state['blades'][0]['length'],1120+extra)
            doc=self.module.generate(p,{'template':self.descriptor,'geometryPurpose':'manufacturing'})
            nodes={n['key']:n for n in doc['geometry']}
            for item in doc['items']:
                if item['key'] in ('frame.left','frame.right') and connection!='face_weld':
                    self.assertEqual(nodes[item['representations']['result']]['arguments']['operation'],'subtract')
                    self.assertEqual(len(item['properties']['louver.part']['slotFeatures']),len(state['blades']))
        state=self.module.layout(self.values(bladeDirectionAngle=45))
        self.assertGreater(len({round(b['length'],4) for b in state['blades']}),1)
        self.assertTrue(any(abs(v)>0.1 for b in state['blades'] for normal,_ in b['planes'] for v in normal))
        doc=self.module.generate(self.values(),{'template':self.descriptor})
        self.assertTrue(any(len(g)>1 for g in doc['extensions']['louver']['partGroups']))

    def test_frames_supports_and_inactive_drafts(self):
        for joint in ('side_wrap','horizontal_wrap','miter'):
            for strategy in ('split','through'):
                p=self.values(frameJoint=joint,middlePostCount=1,middleBeamCount=1,supportMode=strategy,
                              bladeDirectionAngle=30,arrayMode='count',bladeCount=3)
                doc=self.module.generate(p,{'template':self.descriptor,'geometryPurpose':'manufacturing'})
                self.assertEqual(sum(i['key'].startswith(('post.','beam.')) for i in doc['items']),3)
                state=self.module.layout(p)
                self.assertEqual(len(state['arrays']),4 if strategy=='split' else 1)
        base=self.values()
        inactive=self.values(insertDepth=-1,throughExtension=-1,slotClearance=-1,bladeCount=-1,bladeGap=-1,bladeOverlap=-1)
        self.assertEqual(self.module.layout(base)['blades'],self.module.layout(inactive)['blades'])

    def test_invalid_geometry_does_not_pretend_success(self):
        cases=[dict(bladePitch=10),dict(arrayMode='overlap',bladeOverlap=100),
               dict(bladeConnection='slot_insert',insertDepth=39),
               dict(bladeConnection='slot_insert',frameDepth=40),
               dict(bladeConnection='slot_insert',insertDepth=22,middlePostCount=1),
               dict(arrayMode='count',bladeCount=2.5),dict(bladeDirectionAngle=float('nan')),
               dict(width=100,middlePostCount=10),dict(bladeRollAngle=91),
               dict(supportMode='through',middlePostCount=1,bladeDirectionAngle=90,arrayMode='count',bladeCount=1)]
        for changes in cases:
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                self.module.generate(self.values(**changes),{'template':self.descriptor})


if __name__=='__main__':
    unittest.main()
