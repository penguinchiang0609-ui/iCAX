from copy import deepcopy
import json
import math
from pathlib import Path
import tempfile
import unittest
from WindowCatalogueTests import package


@unittest.skip("Deferred product reference; not part of the current active catalogue")
class AluminiumWindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor,cls.defaults,cls.generator=package('aluminium_window')
        cls.engine=cls.generator.module(Path(cls.generator.__file__).with_name('profile_system.py'))
        cls.demo=json.loads((Path(cls.generator.__file__).parent/'systems/demonstration.json').read_text(encoding='utf-8'))

    def build(self,changes=None,system=None,purpose='display'):
        p=dict(self.defaults,**(changes or {}));before=deepcopy(p)
        result=self.generator.generate_system(p,{'template':self.descriptor,'geometryPurpose':purpose},system or self.engine.ProfileSystem(self.demo))
        self.assertEqual(p,before);self.assertEqual(result['parameters'],before)
        return result

    def verified_test_system(self):
        data=deepcopy(self.demo)
        data.update(status='verified',manufacturer='UNIT TEST ONLY',source='Synthetic regression fixture, not production data')
        return self.engine.ProfileSystem(data)

    def test_basic_disassembly_counts_and_material_roles(self):
        for changes,count in [({'windowType':'fixed','screenEnabled':False},4),
                              ({'screenEnabled':False,'trackCount':2},12),({},16),
                              ({'windowType':'hinged','hingedCount':1},8),
                              ({'windowType':'hinged','hingedCount':2},12)]:
            result=self.build(changes)
            assembly=result['extensions']['windowAssembly']
            self.assertEqual(len(assembly['profileParts']),count)
            self.assertFalse(assembly['profileSystem']['productionReady'])
            if changes.get('windowType','sliding')=='sliding':
                ids={part['profileId'] for part in assembly['profileParts']}
                self.assertTrue({'jamb','interlock','slide-top','slide-bottom'}<=ids)
        result=self.build()
        assembly=result['extensions']['windowAssembly']
        self.assertTrue(all('assemblyFrame.member' not in item['properties']
                            for item in result['items']))
        screen=next(p for p in assembly['panels'] if p['type']=='screen')
        self.assertEqual(screen['trackIndex'],2)
        self.assertEqual(sum(p['panelId']==screen['panelId'] for p in assembly['profileParts']),4)

    def test_all_window_layouts_and_single_protocol(self):
        for window in ('fixed','sliding','hinged','mixed'):
            for columns in (1,2,3):
                for rows in (1,2,3):
                    for purpose in ('display','manufacturing'):
                        with self.subTest(window=window,columns=columns,rows=rows,purpose=purpose):
                            result=self.build(dict(windowType=window,columns=columns,rows=rows,width=2400,height=2400,
                                                   cell11='hinged',cell12='sliding',hingedCount=2),self.verified_test_system(),purpose)
                            assembly=result['extensions']['windowAssembly']
                            self.assertEqual(len(assembly['apertures']),columns*rows)
                            self.assertEqual(len(result['outputs']),1)
                            if purpose=='manufacturing':
                                self.assertEqual(len(result['items']),len(assembly['profileParts']))
                                self.assertFalse(any('glass' in i['key'] or 'mesh' in i['key'] for i in result['items']))

    def test_system_rules_drive_sizes_and_roles_not_generator_branches(self):
        system=self.verified_test_system()
        system.data['dimensions']['slidingWidth']='(ApertureWidth - 8 + 30) / 2'
        system.data['constants']['Overlap']=30
        system.data['profiles']['jamb']['material']='TEST-MATERIAL'
        system=self.engine.ProfileSystem(system.data)
        result=self.build({'screenEnabled':False},system,'manufacturing')
        assembly=result['extensions']['windowAssembly']
        self.assertAlmostEqual(assembly['panels'][0]['width'],711)
        self.assertTrue(any(p['material']=='TEST-MATERIAL' for p in assembly['profileParts']))
        system.data['profiles']['jamb']['compatibleProfiles']=[]
        with self.assertRaisesRegex(ValueError,'连接'):
            self.build({'screenEnabled':False},system)

    def test_demo_gate_and_external_file_loading(self):
        for purpose in ('manufacturing',None):
            with self.assertRaisesRegex(ValueError,'生产拆单'):
                self.generator.generate(self.defaults,{'geometryPurpose':purpose})
        with tempfile.TemporaryDirectory(prefix='icax-window-system-') as directory:
            path=Path(directory)/'test-series.json'
            # Deliberately synthetic and only located in this test's temporary directory.
            path.write_text(json.dumps(self.verified_test_system().data),encoding='utf-8')
            p=dict(self.defaults,systemSource='file',systemFile=str(path))
            result=self.generator.generate(p,{'template':self.descriptor,'geometryPurpose':'manufacturing'})
            self.assertTrue(result['extensions']['windowAssembly']['profileSystem']['productionReady'])
            self.assertEqual(len(result['extensions']['windowAssembly']['profileSystem']['digest']),64)
        with self.assertRaises(ValueError):
            self.engine.load_system(dict(self.defaults,systemSource='file',systemFile='../test.json'))

    def test_safe_dimension_expressions(self):
        self.assertEqual(self.engine.dimension('W / 2 - deduction',{'W':1000,'deduction':23.5}),476.5)
        for expression in ('__import__("os")','x.y','[1][0]','2**99999','1/0','Missing+1','1e999',True):
            with self.subTest(expression=expression),self.assertRaises((ValueError,SyntaxError)):
                self.engine.dimension(expression,{})

    def test_machining_features_and_intrinsic_contours(self):
        system=self.verified_test_system()
        system.data['machiningRules']['sliding.bottom']=[{'kind':'roundThroughDepth','station':'Length/2','across':'Face/2','diameter':5}]
        result=self.build({'screenEnabled':False},system,'manufacturing')
        operations=[n for n in result['geometry'] if n['operator']=='boolean' and n['arguments']['operation']=='subtract']
        # Both equal sliding bottom members reuse the same locally machined
        # prototype; there is one cut operation and two independently placed parts.
        self.assertEqual(len(operations),1)
        self.assertEqual(sum(len(p['machiningFeatures']) for p in result['extensions']['windowAssembly']['profileParts']),2)
        system.data['machiningRules']['sliding.bottom'][0]['kind']='invented'
        with self.assertRaisesRegex(ValueError,'未支持'):
            self.build({},system,'manufacturing')
        # A machining draft cannot block or reshape the finished product.
        self.assertEqual(self.build({},system)['geometry'],self.build()['geometry'])

    def test_inactive_drafts_and_track_errors(self):
        baseline=self.build({'windowType':'fixed'})
        retained=self.build({'windowType':'fixed','slidingCount':-1,'screenCount':-1,'trackCount':-1,
                             'hingedCount':-1,'systemFile':'missing.json','columnWeight1':-1,'columnWeight3':-1,'rowWeight1':-1})
        self.assertEqual(baseline['geometry'],retained['geometry'])
        for changes in ({'trackCount':2,'screenEnabled':True},{'width':100},{'columns':4},
                        {'columns':2,'columnWeight1':0},{'screenCount':2},{'glassThickness':20}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.build(changes)

    def test_multiple_panels_and_screen_travel_respects_same_track(self):
        for count in (2,3,4):
            result=self.build({'slidingCount':count,'screenEnabled':False,'trackCount':2})
            for panel in result['extensions']['windowAssembly']['panels']:
                self.assertLessEqual(panel['travelRange'][0],0)
                self.assertGreaterEqual(panel['travelRange'][1],0)
        system=self.verified_test_system()
        system.data['dimensions']['screenWidth']='PanelWidth-Overlap'
        result=self.build({'screenCount':2},system)
        screens=[p for p in result['extensions']['windowAssembly']['panels'] if p['type']=='screen']
        self.assertEqual(len(screens),2)
        self.assertAlmostEqual(screens[0]['travelRange'][1],0)
        self.assertAlmostEqual(screens[1]['travelRange'][0],0)

    def test_transom_toplight_spans_columns_without_extra_mullion(self):
        result=self.build({'windowType':'mixed','columns':2,'rows':2,'mergeTopLight':True,
                           'cell11':'sliding','cell12':'hinged','cell21':'hinged','cell22':'sliding'})
        assembly=result['extensions']['windowAssembly']
        self.assertEqual(len(assembly['apertures']),3)
        top=next(a for a in assembly['apertures'] if a['id']=='aperture.2.1')
        self.assertEqual(top['type'],'fixed')
        self.assertAlmostEqual(top['bounds'][1]-top['bounds'][0],1400)
        self.assertEqual(sum(p['role']=='transom' for p in assembly['profileParts']),1)

    def test_external_assembly_uses_uncut_stock_and_real_frame_junctions(self):
        changes={'windowType':'fixed','screenEnabled':False,'columns':2,'rows':2,
                 'assemblyPlanningMode':'external_templates'}
        result=self.build(changes,self.verified_test_system(),'manufacturing')
        assembly=result['extensions']['windowAssembly']
        self.assertEqual(assembly['assemblyPlanningMode'],'external_templates')
        self.assertEqual(len(assembly['profileParts']),7)
        self.assertEqual(len(result['relationships']),10)
        self.assertEqual(sum(r['properties']['topology']=='L' for r in result['relationships']),4)
        self.assertEqual(sum(r['properties']['topology']=='T' for r in result['relationships']),6)
        self.assertFalse(any(node['operator']=='boolean' for node in result['geometry']))
        stock={item['key']:item for item in result['items']}
        geometry={node['key']:node for node in result['geometry']}
        self.assertEqual(len(stock),7)
        frame_face=self.verified_test_system().profile('frame.left')[1]['face']
        self.assertEqual(stock['frame.left']['properties']['assemblyFrame.member']['start'],
                         [frame_face/2,0,frame_face/2])
        self.assertEqual(stock['frame.left']['properties']['assemblyFrame.member']['end'],
                         [frame_face/2,0,result['parameters']['height']-frame_face/2])
        for item in stock.values():
            props=item['properties']
            frame=props['assemblyFrame.member']
            self.assertEqual(props['tubeDesigner.assemblyPlanning'],{'stockState':'uncut','ready':False})
            self.assertEqual(frame['stockState'],'uncut')
            interval=frame['stockInterval']
            self.assertLess(interval['startStation'],0)
            self.assertGreater(interval['endStation'],frame['axisLength'])
            self.assertAlmostEqual(props['length'],interval['endStation']-interval['startStation'])
            self.assertAlmostEqual(math.dist(frame['start'],frame['end']),frame['axisLength'])
            raw=geometry[item['representations']['result']]
            extrusion=geometry[raw['inputs'][0]]
            self.assertAlmostEqual(math.dist([0,0,0],extrusion['arguments']['vector']),props['length'])
            self.assertEqual(props['tubeDesigner.endProcess']['startCut'],'square')
            self.assertEqual(props['tubeDesigner.endProcess']['endCut'],'square')
        for relation in result['relationships']:
            self.assertEqual(relation['kind'],'assembly')
            self.assertEqual(len(relation['items']),2)
            self.assertTrue(set(relation['items'])<=stock.keys())

            anchors=relation['properties']['participantAnchors']
            self.assertEqual({a['itemKey'] for a in anchors},set(relation['items']))
            self.assertTrue(all(a['centerlinePoint']==relation['properties']['centerlinePoint'] for a in anchors))
            for anchor in anchors:
                frame=stock[anchor['itemKey']]['properties']['assemblyFrame.member']
                if anchor['kind']=='end':
                    expected_allowance=(-frame['stockInterval']['startStation'] if anchor['end']=='start'
                                        else frame['stockInterval']['endStation']-frame['axisLength'])
                    self.assertAlmostEqual(anchor['anchor']['stockAllowance'],expected_allowance)
                    if relation['properties']['topology']=='L':
                        self.assertAlmostEqual(anchor['anchor']['contactInset'],expected_allowance)
                axis=[(frame['end'][i]-frame['start'][i])/frame['axisLength'] for i in range(3)]
                expected=[frame['start'][i]+axis[i]*anchor['localAxialStation'] for i in range(3)]
                for actual,target in zip(expected,relation['properties']['centerlinePoint']):
                    self.assertAlmostEqual(actual,target)
            self.assertEqual([a['kind'] for a in anchors],
                             ['end','end'] if relation['properties']['topology']=='L' else ['side','end'])
        by_key={r['key']:r for r in result['relationships']}
        for key in ('frame.corner.bottom-left','frame.corner.bottom-right',
                    'frame.corner.top-left','frame.corner.top-right',
                    'mullion.1.bottom','mullion.1.top',
                    'transom.1.1.left','transom.1.1.right',
                    'transom.1.2.left','transom.1.2.right'):
            self.assertIn(key,by_key)
        self.assertEqual(by_key['mullion.1.bottom']['items'],['frame.bottom','mullion.1'])
        self.assertEqual(by_key['mullion.1.top']['items'],['frame.top','mullion.1'])

    def test_external_stock_covers_asymmetric_profile_datum(self):
        changes={'windowType':'fixed','screenEnabled':False,
                 'assemblyPlanningMode':'external_templates'}
        baseline=self.build(changes,self.verified_test_system(),'manufacturing')
        system=self.verified_test_system()
        system.data['profiles']['frame']['referenceOrigin']=[10,0]
        shifted=self.build(changes,self.engine.ProfileSystem(system.data),'manufacturing')
        before={item['key']:item for item in baseline['items']}
        after={item['key']:item for item in shifted['items']}
        for key in ('frame.left','frame.right','frame.bottom','frame.top'):
            original=before[key]['properties']['assemblyFrame.member']
            stock=after[key]['properties']['assemblyFrame.member']
            self.assertEqual(stock['start'],original['start'])
            self.assertEqual(stock['end'],original['end'])
            self.assertAlmostEqual(stock['stockInterval']['startStation'],-35)
            self.assertAlmostEqual(stock['stockInterval']['endStation'],stock['axisLength']+35)
        for relation in shifted['relationships']:
            for participant in relation['properties']['participantAnchors']:
                if participant['kind']=='end':
                    self.assertAlmostEqual(participant['anchor']['stockAllowance'],35)
                    if relation['properties']['topology']=='L':
                        self.assertAlmostEqual(participant['anchor']['contactInset'],15)
                        self.assertAlmostEqual(participant['anchor']['stockAllowance']+
                                               participant['anchor']['contactInset'],50)

    def test_external_assembly_covers_sashes_screen_and_merged_top_light(self):
        for family in ('sliding','hinged'):
            with self.subTest(family=family):
                result=self.build({'windowType':family,'assemblyPlanningMode':'external_templates',
                                   'screenEnabled':family=='sliding'},self.verified_test_system(),'display')
                rings=1+(2 if family=='sliding' else 1)+(1 if family=='sliding' else 0)
                self.assertEqual(len(result['relationships']),4*rings)
                self.assertTrue(all(r['properties']['topology']=='L' for r in result['relationships']))
                self.assertFalse(any('glass' in key or 'mesh' in key
                                     for r in result['relationships'] for key in r['items']))
        top=self.build({'windowType':'fixed','columns':2,'rows':2,'mergeTopLight':True,
                        'assemblyPlanningMode':'external_templates'},self.verified_test_system(),'display')
        by_key={r['key']:r for r in top['relationships']}
        self.assertEqual(len(by_key),8)
        self.assertEqual(by_key['mullion.1.top']['items'],['transom.1.1','mullion.1'])
        for merged,expected in ((False,20),(True,16)):
            with self.subTest(merged_top=merged):
                grid=self.build({'windowType':'fixed','columns':3,'rows':3,'mergeTopLight':merged,
                                 'assemblyPlanningMode':'external_templates'},
                                self.verified_test_system(),'display')
                self.assertEqual(len(grid['relationships']),expected)
                self.assertTrue(all(len(r['properties']['participantAnchors'])==2
                                    for r in grid['relationships']))

    def test_external_assembly_ignores_legacy_joint_and_machining_rules(self):
        p={'windowType':'fixed','columns':2,'rows':2,'assemblyPlanningMode':'external_templates'}
        original=self.build(p,self.verified_test_system(),'manufacturing')
        modified=self.verified_test_system().data
        modified['joints']['frame']='side_wrap'
        modified['machiningRules']['frame.left']=[{'kind':'invented'}]
        changed=self.build(p,self.engine.ProfileSystem(modified),'manufacturing')
        self.assertEqual(original['geometry'],changed['geometry'])
        self.assertEqual(original['relationships'],changed['relationships'])
        self.assertFalse(any(part['machiningFeatures'] for part in changed['extensions']['windowAssembly']['profileParts']))
        with self.assertRaisesRegex(ValueError,'未支持'):
            self.build(dict(p,assemblyPlanningMode='legacy_processed'),self.engine.ProfileSystem(modified),'manufacturing')
        with self.assertRaisesRegex(ValueError,'生产拆单'):
            self.build(p,purpose='manufacturing')

    def test_external_assembly_connections_cover_window_layout_matrix(self):
        system=self.verified_test_system()
        for family in ('fixed','sliding','hinged','mixed'):
            for columns in (1,2,3):
                for rows in (1,2,3):
                    with self.subTest(family=family,columns=columns,rows=rows):
                        result=self.build({'windowType':family,'columns':columns,'rows':rows,
                                           'width':2400,'height':2400,'cell11':'hinged',
                                           'cell12':'sliding','hingedCount':2,
                                           'assemblyPlanningMode':'external_templates'},system)
                        tubes={item['key'] for item in result['items']
                               if item['properties'].get('manufacturing.partKind')=='tube'}
                        self.assertGreaterEqual(len(result['relationships']),4)
                        self.assertEqual(len({r['key'] for r in result['relationships']}),
                                         len(result['relationships']))
                        for relation in result['relationships']:
                            self.assertTrue(set(relation['items'])<=tubes)
                            self.assertEqual({a['itemKey'] for a in relation['properties']['participantAnchors']},
                                             set(relation['items']))


if __name__=='__main__':unittest.main()
