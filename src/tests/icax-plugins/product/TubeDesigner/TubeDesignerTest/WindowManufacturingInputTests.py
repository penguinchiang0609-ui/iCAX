"""Stable window design inputs remain independent of assembly route execution."""
import contextlib
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from WindowCatalogueTests import package, SRC
import icax_template_worker as worker

ROOT = SRC/'apps/tube-designer/templates/_shared'
FIELDS = {'schema','schemaVersion','connections','processes'}


def load(name):
    spec = importlib.util.spec_from_file_location('window_input_test_'+name, ROOT/(name+'.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


@contextlib.contextmanager
def forbid_product_processing():
    previous = sys.getprofile()
    forbidden = {
        'security_window_frame_paths.py': {'plan','emit','_prepare','_allocate','_target_span_check','_fold_clearance'},
        'assembly_window_process.py': {'plan','_compile_route','compile_window_assembly'},
        'assembly_geometry_process_runtime.py': {'evaluate','apply','materialize_resolved_tool_plan','apply_frozen_cutters'},
        'assembly_template_runtime.py': {'resolve_process_plan','_template_by_id','evaluate_process','resolve_bound_plan'},
        'assembly_fold_functions.py': {'evaluate_fold','_evaluate'},
        'punch_tool_runtime.py': {'_evaluate','generate','_analyze_mould'},
    }
    def guard(frame, event, unused):
        if event == 'call':
            filename = frame.f_code.co_filename.replace('\\','/')
            source, name = filename.rsplit('/',1)[-1], frame.f_code.co_name
            blocked = name in forbidden.get(source, ())
            blocked |= '/templates/assembly/' in filename and name in ('build_plan','build_bound_operations','build_formed_preview')
            blocked |= '/templates/mold/' in filename and name in ('build','generate','_build')
            if blocked:
                raise AssertionError('product called route/process implementation: '+source+':'+name)
        if previous is not None: previous(frame,event,unused)
    sys.setprofile(guard)
    try: yield
    finally: sys.setprofile(previous)


@contextlib.contextmanager
def forbid_product_callbacks():
    previous = sys.getprofile()
    def guard(frame,event,unused):
        if event == 'call':
            filename = frame.f_code.co_filename.replace('\\','/')
            source, name = filename.rsplit('/',1)[-1],frame.f_code.co_name
            if ('/templates/product/' in filename
                    or source=='product_window_manufacturing.py' and name=='build_window_manufacturing'
                    or source=='security_window_product_contract.py' and name in ('_design','generate')):
                raise AssertionError('downstream called product implementation: '+filename+':'+name)
        if previous is not None: previous(frame,event,unused)
    sys.setprofile(guard)
    try: yield
    finally: sys.setprofile(previous)


def execute(document, design, context=None):
    with forbid_product_callbacks():
        return worker._execute_manufacturing({'manufacturingDefinition':deepcopy(document),
            'designModel':deepcopy(design), 'context':{'sharedRoot':str(ROOT),**(context or {})}})


def allocate(document, design, context=None):
    with forbid_product_callbacks():
        from icax_template_sdk.manufacturing import compose_manufacturing_model
        compiled = load('assembly_window_process').compile_window_assembly(compose_manufacturing_model(document,design))
        return load('assembly_material_allocation').allocate_window_manufacturing(compiled,context or {})


def design_for(changes):
    _, defaults, core = package('single_face_security_window')
    return core.display({**defaults, **changes})


def stable_input(document, design):
    local = document['processes'][0]['definition']['processInput']
    return {'design':deepcopy(design), 'connections':deepcopy(document['connections']),
            'processGeometry':deepcopy(local['geometry']), 'processParts':deepcopy(local['parts'])}


class WindowManufacturingInputTests(unittest.TestCase):
    def build(self, changes):
        _,defaults,core = package('single_face_security_window')
        values = {**defaults,**changes}; saved = deepcopy(values)
        with forbid_product_processing():
            design = core.display(values)
            result = core.manufacturing(values)
        self.assertEqual(values,saved)
        from icax_template_sdk.manufacturing import validate_manufacturing_model, compose_manufacturing_model
        validate_manufacturing_model(result)
        self.assertEqual(result['schemaVersion'],4)
        self.assertEqual(set(result),FIELDS)
        self.assertEqual(set(design), {'schema','schemaVersion','coordinateSystem','lengthUnit',
                                      'resources','items','roots','annotations'})
        self.assertEqual(design['schemaVersion'],2)
        self.assertNotIn('connections',design)
        frozen = deepcopy(design); raw = deepcopy(result)
        combined = compose_manufacturing_model(result,design)
        self.assertEqual(design,frozen); self.assertEqual(result,raw)
        self.assertEqual(combined['schemaVersion'],3)
        self.assertEqual({item['key'] for item in combined['items']},{item['key'] for item in design['items']})
        self.assertTrue(all('assemblyFrame.member' in item['properties'] for item in design['items']))
        self.assertEqual(len(result['processes']),1)
        definition = result['processes'][0]['definition']
        self.assertEqual(definition['templateId'],'security-window-assembly')
        self.assertNotIn('targets',definition)
        self.assertEqual(definition['outputs'],{'manufacturing':{'kind':'manufacturing-set','key':'window.manufacturing'}})
        self.assertEqual(definition['processInput']['schemaVersion'],3)
        self.assertEqual(definition['processInput']['parts'],{'members':{'scope':'design','itemKeys':design['roots']}})
        self.assertEqual(set(definition['processInput']['geometry']),{'layout','mergeIdenticalParts'})
        self.assertIs(definition['processInput']['geometry']['mergeIdenticalParts'],True)
        self.assertEqual(definition['dependencies'],[])
        return result

    def test_faces_routes_and_opening_frames_do_not_execute_processes(self):
        for face in ('single','two','three','five'):
            for mode in ('segment_weld','plane_v_notch','spatial_v_notch'):
                for opening in (False,True):
                    with self.subTest(face=face,mode=mode,opening=opening):
                        self.build({'faceType':face,'frameManufacturingMode':mode,'accessDoorEnabled':opening,
                                    'doorFrameJoinType':'v_groove_90:tool_library','doorLeafFrameJoinType':'v_groove_90:tool_library'})

    def test_all_thirteen_public_products_work_with_route_and_processing_functions_blocked(self):
        baselines = json.loads((SRC.parent/'output/tests/assembly-process/manufacturing-before.json').read_text(encoding='utf-8'))
        self.assertEqual(len(baselines),13)
        from icax_template_sdk.manufacturing import validate_manufacturing_model
        for baseline in baselines:
            with self.subTest(product=baseline['name']):
                _,_,core = package(baseline['name'])
                values = deepcopy(baseline['parameters']); saved = deepcopy(values)
                with forbid_product_processing(): result = core.manufacturing(values)
                validate_manufacturing_model(result)
                self.assertEqual(result['schemaVersion'],4)
                self.assertEqual(values,saved)

    def test_continuous_frame_uses_the_host_display_members_without_duplicate_geometry(self):
        changes = {'frameManufacturingMode':'plane_v_notch'}
        result = self.build(changes); design = design_for(changes)
        outer = [item for item in design['items'] if item['key'].startswith('outer_frame.')]
        self.assertEqual(len(outer),4)
        self.assertFalse({'items','resources','roots','sourceMappings'} & set(result))
        self.assertFalse(any('continuous' in item['key'] for item in design['items']))
        self.assertEqual(result['processes'][0]['definition']['parameters']['frameManufacturingMode'],'plane_v_notch')

    def test_route_mould_k_and_joint_drafts_do_not_change_design_input(self):
        for opening in (False,True):
            common = {'faceType':'three','accessDoorEnabled':opening}
            reference = stable_input(self.build(common),design_for(common))
            for changes in (
                    {'frameManufacturingMode':'plane_v_notch'},
                    {'frameManufacturingMode':'spatial_v_notch'},
                    {'frameManufacturingMode':'plane_v_notch','tubeDesignerToolBindings':{
                        'outerFrameGroove':{'ref':{'scope':'system','id':'edge-arc-groove'},'parameters':{'leftArc':True}}}},
                    {'frameManufacturingMode':'plane_v_notch','tubeDesignerToolBindings':{
                        'outerFrameGroove':{'ref':{'scope':'system','id':'v-notch-sharp'},
                                            'parameters':{'bottomStrategy':'rounded','bendCompensation':True,'kFactor':.35}}}},
                    {'assemblyClearance':.7,'horizontalBranchReserve':9,'verticalBranchReserve':12,
                     'frameCornerJoin':'rail_miter','doorFrameJoinType':'v_groove_90:tool_library',
                     'doorLeafFrameJoinType':'v_groove_90:tool_library'}):
                with self.subTest(opening=opening,changes=changes):
                    selected = {**common,**changes}
                    self.assertEqual(stable_input(self.build(selected),design_for(selected)),reference)

    def test_same_three_face_input_produces_different_manufacturing_sets_downstream(self):
        changes = {'faceType':'three','accessDoorEnabled':False,
                   'verticalMaximumCenterSpacing':600,'sideVerticalMaximumCenterSpacing':600,
                   'topBottomRodMaximumCenterSpacing':600,'frameManufacturingMode':'plane_v_notch'}
        raw = self.build(changes); design = design_for(changes)
        saved = deepcopy(raw)
        plane = execute(raw,design)
        spatial_raw = deepcopy(raw)
        spatial_raw['processes'][0]['definition']['parameters']['frameManufacturingMode'] = 'spatial_v_notch'
        self.assertEqual(stable_input(raw,design),stable_input(spatial_raw,design))
        spatial = execute(spatial_raw,design)
        self.assertEqual(raw,saved)
        self.assertEqual(len(plane['items']),25)
        self.assertEqual(len(spatial['items']),22)
        self.assertEqual(sum(item['key'].startswith('outer_frame.') for item in plane['items']),6)
        self.assertEqual(sum(item['key'].startswith('outer_frame.') for item in spatial['items']),3)
        from icax_template_sdk.manufacturing import compose_manufacturing_model
        compiled_plane = load('assembly_window_process').compile_window_assembly(compose_manufacturing_model(raw,design))
        compiled_spatial = load('assembly_window_process').compile_window_assembly(compose_manufacturing_model(spatial_raw,design))
        self.assertEqual(len([item for item in compiled_plane['items'] if item['key'].startswith('outer_frame.plane.')]),2)
        self.assertEqual(len([item for item in compiled_spatial['items'] if item['key']=='outer_frame.spatial.continuous']),1)
        self.assertTrue(any(item['manufacturingInput']['path']['closed'] for item in compiled_spatial['items']))

    def test_downstream_rebuilds_all_source_inputs_without_product_callback(self):
        for face in ('single','two','three','five'):
            for mode in ('segment_weld','plane_v_notch','spatial_v_notch'):
                for opening in (False,True):
                    changes = {'faceType':face,'frameManufacturingMode':mode,'accessDoorEnabled':opening,
                        'doorFrameJoinType':'v_groove_90:tool_library','doorLeafFrameJoinType':'v_groove_90:tool_library'}
                    with self.subTest(changes=changes):
                        result = self.build(changes); saved = deepcopy(result)
                        executed = execute(result,design_for(changes))
                        self.assertEqual(result,saved)
                        source = executed['extensions']['tubeDesigner.assemblyProcessSource']
                        self.assertEqual(len(source['stocks']),len(executed['items']))
                        self.assertTrue(all(item['properties']['length']>0 for item in executed['items']))
                        self.assertTrue(all(instance['processInput']['schemaVersion']==1 for instance in source['instances']))
                        self.assertEqual(executed['extensions']['tubeDesigner.manufacturingPartCount'],len(executed['items']))

    def test_compensation_is_resolved_downstream_and_not_assumed_zero(self):
        # The current product owns these explicit fields; an old binding draft
        # cannot override a public zero K. Compensation applies to edge arc.
        changes = {'frameManufacturingMode':'plane_v_notch',
            'outerFrameGrooveTool': 'system:edge-arc-groove', 'outerFrameBendKFactor': .35}
        result = self.build(changes)
        plan = next(iter(allocate(result,design_for(changes))['allocationPlans'].values()))
        self.assertGreater(load('security_window_process_adapter').bend_allowance(plan),0)
        self.assertEqual(plan['targetSpanCheck']['status'], 'pass')

    def test_default_allocation_preserves_legacy_lengths_and_material_categories(self):
        descriptor,defaults,core = package('single_face_security_window')
        result = self.build({})
        current = {item['key']:item['properties'] for item in execute(result,core.display(defaults))['items']}
        baseline = core.generate(defaults,{'template':descriptor,'geometryPurpose':'manufacturing'})
        old = {item['key']:item['properties'] for item in baseline['items']}
        self.assertEqual(set(old),set(current))
        for key,props in current.items():
            with self.subTest(item=key):
                self.assertAlmostEqual(props['length'],old[key]['length'],places=9)
                for field in ('manufacturing.categoryKey','manufacturing.categoryName',
                              'manufacturing.material','manufacturing.materialGrade','material','materialGrade'):
                    self.assertEqual(props.get(field),old[key].get(field))

    def test_continuous_frame_downstream_keeps_all_contacts_and_splits_only_seam_contact(self):
        changes = {'frameManufacturingMode':'plane_v_notch','accessDoorEnabled':False,
            'horizontalMaximumCenterSpacing':500,'verticalMaximumCenterSpacing':600,
            'firstHorizontalTopOffset':200,'lastHorizontalBottomOffset':200,
            'verticalLeftCenterOffset':110,'verticalRightCenterOffset':110}
        result = self.build(changes)
        self.assertEqual(len(result['processes']),1)
        source = execute(result,design_for(changes))['extensions']['tubeDesigner.assemblyProcessSource']
        actual = [call for call in source['instances'] if call['templateId']=='tube-profile-aperture'
                  and call['targets']['stock']=='outer_frame.continuous.0001']
        self.assertEqual(len(actual),15)
        seam = [call for call in actual if 'outer_frame.bottom.' in call['instanceId']
                and call['processInput']['parts']['branch']['id']=='main_grid.vertical.0002']
        self.assertEqual(len(seam),2)
        self.assertEqual({tuple(call['processInput']['geometry']['clipInterval']) for call in seam},
                         {(0.,599.),(5393.,5992.)})

    def test_user_groove_resource_keeps_its_actual_non_v_geometry_and_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            identifier = 'user-edge-fixture'; package_root = Path(directory)/identifier
            package_root.mkdir(); original = ROOT.parent/'mold/edge-arc-groove'
            descriptor = json.loads((original/'tool.json').read_text(encoding='utf-8'))
            descriptor['id'] = identifier
            (package_root/'tool.json').write_text(json.dumps(descriptor),encoding='utf-8')
            shutil.copyfile(original/'tool.py',package_root/'tool.py')
            changes = {'frameManufacturingMode':'plane_v_notch','accessDoorEnabled':False,
                'tubeDesignerToolBindings':{'outerFrameGroove':{'ref':{'scope':'user','id':identifier},
                                                              'parameters':{'leftArc':True}}}}
            result = self.build(changes)
            plan = next(iter(allocate(result,design_for(changes),{'userMouldRoot':directory})['allocationPlans'].values()))
            feature = plan['operations'][0]['requestFeature']
            self.assertEqual(feature['toolRef']['scope'],'user')
            self.assertEqual(feature['toolRef']['id'],identifier)
            self.assertTrue(feature['toolRef']['digest'])
            self.assertTrue(feature['toolParameters']['leftArc'])
            self.assertIn('bridge',feature['toolParameters'])
            self.assertNotIn('bottomStrategy',feature['toolParameters'])
            self.assertEqual(plan['targetSpanCheck']['status'],'pass')


if __name__=='__main__': unittest.main()
