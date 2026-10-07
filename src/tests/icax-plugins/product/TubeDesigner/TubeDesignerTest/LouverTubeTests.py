"""Geometry/contract tests for fixed tube louvers, independent of native BRep tests."""
from copy import deepcopy
import math
import unittest
from WindowCatalogueTests import package


@unittest.skip("Deferred product reference; not part of the current active catalogue")
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

    def test_external_axis_aligned_blades_are_uncut_l_and_t_members(self):
        for angle in (0, 90, -90, 180, -180):
            for purpose in ('display', 'manufacturing'):
                with self.subTest(angle=angle, purpose=purpose):
                    values = self.values(assemblyPlanningMode='external_templates',
                                         bladeDirectionAngle=angle, arrayMode='count',
                                         bladeCount=3)
                    original = deepcopy(values)
                    result = self.module.generate(values, {'template': self.descriptor,
                                                            'geometryPurpose': purpose})
                    self.assertEqual(values, original)
                    self.assertEqual(result['parameters'], original)
                    self.assertEqual(result['extensions']['louver']['assemblyPlanningMode'],
                                     'external_templates')
                    self.assertFalse(result['extensions']['louver']['assemblyCuttingReady'])
                    self.assertEqual(len(result['items']), 7)
                    self.assertFalse(any(node['operator'] == 'boolean'
                                         for node in result['geometry']))
                    self.assertEqual([r['properties']['topology']
                                     for r in result['relationships']],
                                     ['L'] * 4 + ['T'] * 6)
                    frame_face = next(item for item in result['items']
                                      if item['key'] == 'frame.left')['properties']['tubeDesigner.profile']['width']
                    for item in result['items']:
                        properties = item['properties']
                        self.assertEqual(properties['tubeDesigner.assemblyPlanning'],
                                         {'stockState': 'uncut', 'ready': False})
                        frame = properties['assemblyFrame.member']
                        self.assertEqual(frame['stockState'], 'uncut')
                        self.assertEqual(frame['start'], properties['louver.part']['start'])
                        self.assertEqual(frame['end'], properties['louver.part']['end'])
                        interval = frame['stockInterval']
                        self.assertLess(interval['startStation'], 0)
                        self.assertGreater(interval['endStation'], frame['axisLength'])
                        self.assertAlmostEqual(properties['length'],
                                               interval['endStation']-interval['startStation'])
                        self.assertAlmostEqual(math.dist(frame['start'], frame['end']),
                                               frame['axisLength'])
                        self.assertEqual(properties['tubeDesigner.endProcess'],
                                         {'startCut': 'square', 'endCut': 'square',
                                          'lengthBasis': 'blank_axial_extent'})
                        self.assertEqual(item['representations']['result'].split('.')[-1],
                                         'solid')
                    left = next(item for item in result['items']
                                if item['key'] == 'frame.left')['properties']['assemblyFrame.member']
                    self.assertEqual(left['start'], [frame_face/2, 0, frame_face/2])
                    self.assertEqual(left['end'], [frame_face/2, 0, values['height']-frame_face/2])
                    frame_profiles = [item['properties']['tubeDesigner.profile']['contours']
                                      for item in result['items'] if item['key'] in ('frame.left', 'frame.bottom')]
                    self.assertEqual(frame_profiles[0], frame_profiles[1])
                    self.assert_external_anchors(result)

    def assert_external_anchors(self, result):
        items = {item['key']: item for item in result['items']}
        geometry = {node['key']: node for node in result['geometry']}
        for relation in result['relationships']:
            point = relation['properties']['centerlinePoint']
            participants = relation['properties']['participantAnchors']
            self.assertEqual([anchor['itemKey'] for anchor in participants], relation['items'])
            for participant in participants:
                item = items[participant['itemKey']]
                props = item['properties']
                start = props['louver.part']['start']
                axis = props['tubeDesigner.manufacturingStartToEnd']
                frame = props['assemblyFrame.member']
                interval = frame['stockInterval']
                station = participant['localAxialStation']
                expected = [start[i] + axis[i] * station for i in range(3)]
                solid = geometry[item['representations']['result']]
                actual_origin = solid['arguments']['placement']['origin']
                physical_start = [start[i] + axis[i]*interval['startStation'] for i in range(3)]
                section = frame['sectionFrame']
                x_axis, y_axis = section['xAxis'], section['yAxis']
                cross = [x_axis[1]*y_axis[2]-x_axis[2]*y_axis[1],
                         x_axis[2]*y_axis[0]-x_axis[0]*y_axis[2],
                         x_axis[0]*y_axis[1]-x_axis[1]*y_axis[0]]
                for coordinate in range(3):
                    self.assertAlmostEqual(expected[coordinate], point[coordinate],
                                           places=6, msg=relation['key'])
                    self.assertAlmostEqual(actual_origin[coordinate], physical_start[coordinate],
                                           places=6, msg=item['key'])
                    self.assertAlmostEqual(cross[coordinate], axis[coordinate])
                    self.assertAlmostEqual(
                        section['originAtStart'][coordinate]
                        + x_axis[coordinate]*section['centerlineUV'][0]
                        + y_axis[coordinate]*section['centerlineUV'][1], start[coordinate])
                    self.assertAlmostEqual(
                        section['originAtStart'][coordinate]
                        + axis[coordinate]*interval['startStation'], actual_origin[coordinate])
                extrusion = geometry[solid['inputs'][0]]
                self.assertAlmostEqual(math.dist([0,0,0],extrusion['arguments']['vector']),
                                       props['length'], places=6)
                self.assertEqual(participant['centerlinePoint'], point)
                if participant['kind'] == 'end':
                    expected_allowance = (-interval['startStation'] if participant['end'] == 'start'
                                          else interval['endStation']-frame['axisLength'])
                    self.assertAlmostEqual(participant['anchor']['stockAllowance'],expected_allowance)
                    if relation['properties']['topology'] == 'L':
                        self.assertAlmostEqual(participant['anchor']['contactInset'],expected_allowance)
                if participant['kind'] == 'side':
                    self.assertGreater(station, 0)
                    self.assertLess(station, props['length'])
                    normal = participant['faceNormal']
                    self.assertEqual(section['faceNormals'][participant['face']], normal)
                    contact = participant['contactPoint']
                    for coordinate in range(3):
                        self.assertAlmostEqual(contact[coordinate],
                                               point[coordinate] + normal[coordinate]
                                               * props['tubeDesigner.profile']['width'] / 2,
                                               places=6)

    def test_external_split_supports_have_complete_t_nodes(self):
        for angle in (0, 90):
            with self.subTest(angle=angle):
                values = self.values(assemblyPlanningMode='external_templates',
                                     middlePostCount=1, middleBeamCount=1,
                                     bladeDirectionAngle=angle, arrayMode='count',
                                     bladeCount=2)
                result = self.module.generate(values, {'template': self.descriptor,
                                                        'geometryPurpose': 'manufacturing'})
                blades = [item for item in result['items']
                          if item['key'].startswith('blade.')]
                self.assertEqual(len(blades), 8)
                self.assertEqual(len(result['items']), 4 + 1 + 2 + 8)
                self.assertEqual(len(result['relationships']),
                                 4 + 2 * (1 + 2 + 8))
                self.assertFalse(any(node['operator'] == 'boolean'
                                     for node in result['geometry']))
                self.assert_external_anchors(result)
                by_key = {relation['key']: relation for relation in result['relationships']}
                self.assertEqual(by_key['beam.1.0.right']['items'][0], 'post.1')
                self.assertEqual(by_key['beam.1.1.left']['items'][0], 'post.1')
                self.assertEqual(by_key['post.1.bottom']['items'][0], 'frame.bottom')

    def test_external_ignores_inactive_cutting_drafts_but_returns_input(self):
        base = self.values(assemblyPlanningMode='external_templates')
        changed = dict(base, frameJoint='miter', endClearance=-1,
                       insertDepth=-1, throughExtension=-1, slotClearance=-1)
        for purpose in ('display', 'manufacturing'):
            with self.subTest(purpose=purpose):
                context = {'template': self.descriptor, 'geometryPurpose': purpose}
                expected = self.module.generate(base, context)
                actual = self.module.generate(changed, context)
                self.assertEqual(actual['parameters'], changed)
                self.assertEqual(actual['geometry'], expected['geometry'])
                self.assertEqual(actual['items'], expected['items'])
                self.assertEqual(actual['relationships'], expected['relationships'])

    def test_external_rejects_unexpressed_crossings_and_cuts(self):
        cases = (
            ({'bladeDirectionAngle': 30}, '斜向'),
            ({'bladeConnection': 'slot_insert'}, '插槽'),
            ({'bladeConnection': 'through_insert'}, '穿透'),
            ({'middlePostCount': 1, 'supportMode': 'through'}, '开槽'),
            ({'startOffset': 0, 'equalEdgeMargin': False}, '第三侧'),
        )
        for changes, message in cases:
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, message):
                self.module.generate(self.values(assemblyPlanningMode='external_templates',
                                                 **changes), {'template': self.descriptor,
                                                              'geometryPurpose': 'manufacturing'})

    def test_legacy_mode_still_applies_requested_miter_and_slots(self):
        values = self.values(assemblyPlanningMode='legacy_processed',
                             frameJoint='miter', bladeConnection='slot_insert')
        result = self.module.generate(values, {'template': self.descriptor,
                                                'geometryPurpose': 'manufacturing'})
        self.assertEqual(result['parameters'], values)
        self.assertTrue(any(node['operator'] == 'boolean'
                            for node in result['geometry']))
        self.assertTrue(any(item['properties']['louver.part'].get('miter')
                            for item in result['items']))
        self.assertTrue(all('assemblyFrame.member' not in item['properties']
                            for item in result['items']))


if __name__=='__main__':
    unittest.main()
