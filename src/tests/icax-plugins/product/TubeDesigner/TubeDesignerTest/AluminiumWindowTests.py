from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
from WindowCatalogueTests import package


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
        assembly=self.build()['extensions']['windowAssembly']
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
        self.assertEqual(len(operations),2)
        self.assertEqual(sum(len(p['machiningFeatures']) for p in result['extensions']['windowAssembly']['profileParts']),2)
        system.data['machiningRules']['sliding.bottom'][0]['kind']='invented'
        with self.assertRaisesRegex(ValueError,'未支持'):
            self.build({},system)

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


if __name__=='__main__':unittest.main()
