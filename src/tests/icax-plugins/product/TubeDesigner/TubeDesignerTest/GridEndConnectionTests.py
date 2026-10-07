"""Grid end choices consume real sections and keep the shared display frozen."""
from copy import deepcopy
import hashlib
import io
import itertools
import json
import sys
import time
import unittest

from WindowManufacturingInputTests import package, load, ROOT
from icax_template_sdk.manufacturing import compose_manufacturing_model
from FoldedPostConnectionTests import allocate, full_plan


def window(horizontal='insert', vertical='insert', **changes):
    _, defaults, core = package('single_face_security_window')
    values = {**deepcopy(defaults), 'width':600, 'height':800, 'accessDoorEnabled':False,
              'horizontalMaximumCenterSpacing':1000, 'verticalMaximumCenterSpacing':600,
              'horizontalEndConnection':horizontal, 'verticalEndConnection':vertical,
              'assemblyClearance':.1, **changes}
    before=deepcopy(values)
    design, declaration=core.display(values),core.manufacturing(values)
    assert values==before
    compiled=load('assembly_window_process').compile_window_assembly(compose_manufacturing_model(declaration,design))
    allocation=allocate(compiled)
    return values,design,compiled,allocation


class GridEndConnectionTests(unittest.TestCase):
    def test_all_choices_have_actual_end_and_receiver_tools_and_correct_lengths(self):
        reference=None
        for horizontal,vertical in itertools.product(('insert','weld','tabs'),repeat=2):
            with self.subTest(horizontal=horizontal,vertical=vertical):
                values,design,compiled,allocation=window(horizontal,vertical)
                if reference is None: reference=design
                self.assertEqual(design,reference)
                items={item['key']:item for item in allocation['model']['items']}
                instances=allocation['assemblySource']['instances']
                for role,choice,dimension,tablength in (
                    ('horizontal',horizontal,'width',12.),('vertical',vertical,'height',8.)):
                    key='main_grid.'+role+'.0001'
                    part=items[key]
                    reserve=values[role+'BranchReserve'] if choice=='insert' else tablength if choice=='tabs' else 0.
                    self.assertAlmostEqual(part['properties']['length'],values[dimension]-2*values['frameWidth']+2*reserve)
                    endcalls=[call for call in instances if call['targets'].get('stock')==key
                              and call['templateId'] in ('tube-end-joint','tube-post-end')
                              and 'material-boundary' not in call['instanceId']]
                    self.assertEqual(len(endcalls),2)
                    self.assertTrue(all(call['templateId']==('tube-post-end' if choice=='tabs' else 'tube-end-joint') for call in endcalls))
                    mothers=[call for call in instances if call['templateId']=='tube-post-aperture'
                             and call['processInput']['parts']['branch']['id']==key]
                    self.assertEqual(len(mothers),2 if choice=='tabs' else 0)
                    ordinary=[call for call in instances if call['templateId']=='tube-profile-aperture'
                              and call['processInput']['parts']['branch']['id']==key
                              and call['targets']['stock'].startswith('outer_frame.')]
                    self.assertEqual(len(ordinary),2 if choice=='insert' else 0)
                plan=full_plan(allocation)
                self.assertTrue(plan['operations'])
                # The real vertical-through-horizontal grid intersection remains.
                self.assertTrue(any(call['templateId']=='tube-profile-aperture'
                    and call['targets']['stock']=='main_grid.horizontal.0001'
                    for call in instances))

    def test_weld_ignores_inactive_reserves_and_clearance_for_its_end_trims(self):
        a=window('weld','weld',infillPattern='horizontal',horizontalBranchReserve=999,
                 verticalEndConnection='inactive-draft',verticalBranchReserve=-99,assemblyClearance=999)
        full_plan(a[-1])
        for call in a[-1]['assemblySource']['instances']:
            if call['templateId']=='tube-end-joint' and call['targets']['stock'].startswith('main_grid.'):
                self.assertEqual(call['parameters'],{'insertionDepth':0.,'clearance':0.})

    def test_curved_ears_and_matching_shell_slots_use_actual_saved_circle(self):
        allocation=window('tabs','tabs')[-1]
        runtime=load('assembly_template_runtime')
        for call in allocation['assemblySource']['instances']:
            if call['templateId'] not in ('tube-post-end','tube-post-aperture'): continue
            role='stock' if call['templateId']=='tube-post-end' else 'branch'
            part=call['processInput']['parts'][role]
            if not part['id'].startswith('main_grid.vertical.'): continue
            shell=load('section_geometry').closed_shell_metrics(load('section_geometry').from_profile(part['section']['profile']))
            self.assertTrue(shell['circularShell'])
            before=deepcopy(call)
            result=runtime.evaluate_process(call['templateId'],call['processInput'],call['parameters'],{})
            self.assertTrue(result['applicable'],result['reason'])
            self.assertEqual(call,before)
            if call['templateId']=='tube-post-aperture':
                profile=next(node for node in result['geometry'] if node['key']=='round-socket.shell-profile')
                generated=load('section_geometry').closed_shell_metrics(load('section_geometry').from_profile(profile['arguments']))
                self.assertAlmostEqual(generated['contours'][0]['edges'][0]['radius'],shell['contours'][0]['edges'][0]['radius']+.1)
                self.assertAlmostEqual(generated['contours'][1]['edges'][0]['radius'],shell['contours'][1]['edges'][0]['radius']-.1)
                self.assertAlmostEqual(generated['contours'][0]['edges'][0]['center'][0],0.)
                self.assertAlmostEqual(generated['contours'][0]['edges'][0]['center'][1],0.)

    def test_folded_receivers_observe_true_span_coordinates(self):
        allocation=window('tabs','tabs',frameManufacturingMode='plane_v_notch')[-1]
        calls=[call for call in allocation['assemblySource']['instances'] if call['templateId']=='tube-post-aperture']
        self.assertTrue(calls)
        self.assertTrue(all('clipInterval' in call['processInput']['geometry'] for call in calls))
        self.assertTrue(full_plan(allocation)['operations'])

    def test_opening_receivers_and_other_faces_keep_leaf_connection_independent(self):
        for face in ('single','two','three','five'):
            with self.subTest(face=face):
                changes={'faceType':face,'width':1200,'height':1800,'accessDoorEnabled':True,
                         'sideVerticalMaximumCenterSpacing':600,'topBottomRodMaximumCenterSpacing':600}
                reference=window('insert','insert',**changes)
                actual=window('tabs','weld',**changes)
                self.assertEqual(reference[1],actual[1])
                def leaf(plan):
                    return {item['key']:item['properties'] for item in plan['model']['items']
                            if item['key'].startswith('access_door.leaf.')}
                self.assertEqual(leaf(reference[-1]),leaf(actual[-1]))
                selected=[call for call in actual[-1]['assemblySource']['instances']
                          if call['templateId']=='tube-post-aperture' and call['targets']['stock'].startswith('access_door.fixed_frame.')]
                self.assertTrue(selected)
                self.assertTrue(full_plan(actual[-1])['operations'])


if __name__=='__main__':
    started=time.perf_counter()
    log=io.StringIO()
    result=unittest.TextTestRunner(stream=log,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(GridEndConnectionTests))
    print(log.getvalue())
    report=ROOT.parents[4]/'output/tests/branch-end-connections/python-manufacturing.json'
    report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps({'passed':result.wasSuccessful(),'testsRun':result.testsRun,
        'elapsedSeconds':time.perf_counter()-started,'gridChoiceMatrix':9,'openingFaceCases':4,
        'log':log.getvalue(),'sourceHashes':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
            'product_window_manufacturing.py','assembly_window_process.py','assembly_post_machining.py','assembly_material_allocation.py')}},
        ensure_ascii=False,indent=2),encoding='utf-8')
    sys.exit(0 if result.wasSuccessful() else 1)
