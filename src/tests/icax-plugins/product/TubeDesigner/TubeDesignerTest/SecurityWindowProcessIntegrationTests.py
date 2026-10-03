"""Window material allocation uses repeatable processes and exact source data."""
from copy import deepcopy
import json
import inspect
import math
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from SecurityWindowFrameManufacturingTests import SUBJECT, DEFAULTS, DESCRIPTOR, generate as generate_public
from AssemblyProcessReuseTests import runtime, example_input
from icax_template_sdk import NeutralModel, expand_resource_model


ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'src/apps/tube-designer/templates').is_dir())
TEMPLATES = ROOT / 'src/apps/tube-designer/templates'


def generate(face, mode, purpose='manufacturing', **extra):
    if purpose == 'display':
        values = {**deepcopy(DEFAULTS), 'faceType': face, 'frameManufacturingMode': mode, **extra}
        original = deepcopy(values)
        result = SUBJECT.display(values)
        assert values == original and result['schema'] == 'icax.display-model'
        assert 'parameters' not in result and 'template' not in result
        return expand_resource_model(result)
    return expand_resource_model(generate_public(face, mode, purpose, **extra))


def source(document):
    return document['extensions']['tubeDesigner.assemblyProcessSource']


def process_items(document):
    return {i['properties']['tubeDesigner.assemblyProcessStockId']: i
            for i in document['items'] if 'tubeDesigner.assemblyProcessStockId' in i['properties']}


class SecurityWindowProcessIntegrationTests(unittest.TestCase):
    def test_folded_side_observers_keep_physical_members_and_independent_actual_sections(self):
        for width, depth in ((38,38),(40,25)):
            with self.subTest(width=width,depth=depth):
                document = generate('single','plane_v_notch',frameWidth=width,frameDepth=depth,
                                    accessDoorEnabled=False)
                declaration = source(document)
                folded = next(stock for stock in declaration['stocks'] if '.continuous.' in stock['id'])
                stock_id = folded['id']
                allocated_ids = {stock['id'] for stock in declaration['stocks']}
                self.assertNotIn('.observed.', stock_id)
                observers = [part for instance in declaration['instances']
                             for role,part in instance['processInput']['parts'].items()
                             if role == 'mate' and part['id'] == stock_id]
                self.assertTrue(observers)
                for part in observers:
                    self.assertEqual(stock_id,part['id'])
                    self.assertTrue(part['profileRef']['id'].startswith(stock_id+'.observed.'))
                    self.assertNotEqual(folded['profileRef'],part['profileRef'])
                    profile = part['section']['profile']
                    self.assertTrue(profile['contours'])
                    self.assertEqual(sorted((width,depth)),sorted((profile['width'],profile['depth'])))
                # A reference denotes one exact local section. Observing a
                # folded side cannot reuse the unfolded stock's section key.
                sections = {}
                for instance in declaration['instances']:
                    self.assertIn(instance['targets']['stock'],allocated_ids)
                    for part in instance['processInput']['parts'].values():
                        section = part['section']
                        contours = section.get('profile',section)['contours']
                        key = json.dumps({'ref':part['profileRef'],'parameters':part.get('parameters',{})},
                                         sort_keys=True)
                        if key in sections:
                            self.assertEqual(sections[key],contours,part['profileRef'])
                        else:
                            sections[key] = deepcopy(contours)
                # Even the square section uses different parameterization in
                # the vertical observed basis, which must remain untouched.
                raw = process_items(document)[stock_id]['properties']['tubeDesigner.assemblyProcessStockProfile']
                self.assertTrue(any(part['section']['profile']['contours'] != raw['contours'] for part in observers))

    def test_fold_adapters_delegate_cutter_graphs_and_operations_to_executor(self):
        emit = NeutralModel.geometry
        product_files = {"security_window_process_adapter.py", "security_window_frame_geometry.py",
                         "security_window_frame_paths.py", "security_window_product_contract.py",
                         "multi_face_security_window.py", "template.py"}
        calls = []
        def check_boundary(model, key, operator, *args, **kwargs):
            caller = Path(inspect.currentframe().f_back.f_code.co_filename).name
            if ".export.groove." in key or operator == "boolean":
                self.assertNotIn(caller, product_files, key)
                calls.append((caller,key,operator))
            return emit(model, key, operator, *args, **kwargs)
        with mock.patch.object(NeutralModel, "geometry", check_boundary):
            for face, mode in (("single","plane_v_notch"),("three","spatial_v_notch"),
                               ("five","plane_v_notch")):
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode, accessDoorEnabled=True,
                                        doorFrameJoinType="v_groove_90:tool_library",
                                        doorLeafFrameJoinType="v_groove_90:tool_library")
                    self.assertTrue(document["extensions"]["tubeDesigner.assemblyGeometryProcesses"]["instances"])
                    self.assertFalse(any(item["properties"].get("tubeDesigner.assemblyProcessSecondaryCutters")
                                         for item in document["items"]))
        self.assertTrue(any(".export.groove." in key for _,key,_ in calls))
        self.assertTrue(any(operator == "boolean" for _,_,operator in calls))

    def test_all_fold_paths_reuse_one_function_per_actual_station(self):
        for face in ('single', 'two', 'three', 'five'):
            for mode in ('plane_v_notch', 'spatial_v_notch'):
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode, accessDoorEnabled=False)
                    declaration, items = source(document), process_items(document)
                    raw = {p['id']: p for p in declaration['stocks']}
                    self.assertEqual(set(raw), set(items))
                    folds = [i for i in declaration['instances'] if i['templateId'] == 'node-v-notch-integrated']
                    self.assertTrue(folds)
                    plan = runtime.resolve_process_plan(declaration['stocks'], declaration['instances'])
                    self.assertEqual(len(raw), len(plan['manufacturingParts']))
                    self.assertEqual(len(folds), len(plan['forming']))
                    for instance in folds:
                        local = instance['processInput']
                        self.assertEqual({'stock'}, set(local['parts']))
                        self.assertNotIn('shapeId', local)
                        self.assertEqual({'stock': local['parts']['stock']['id']}, instance['targets'])
                        self.assertEqual(raw[instance['targets']['stock']]['length'], local['parts']['stock']['length'])
                        self.assertEqual([local['geometry']['station'], 0, 0], local['geometry']['frame']['origin'])
                    for stock_id, item in items.items():
                        props = item['properties']
                        blank = next(p['request'] for p in plan['manufacturingParts'] if p['stockId'] == stock_id)
                        trim = sum(end.get('trim', 0) for end in blank['ends'].values())
                        self.assertAlmostEqual(raw[stock_id]['length']-trim, props['length'], places=3)
                        self.assertTrue(props['manufacturing.sourceMembers'])
                        self.assertIn('tubeDesigner.assemblyProcessStockProfile', props)
                        # The native secondary phase must not repeat any V cut.
                        roots = props['tubeDesigner.assemblyProcessSecondaryCutters']
                        self.assertTrue(all('.groove.' not in key for key in roots))
                        self.assertTrue(set(roots) <= {n['key'] for n in document['geometry']})

    def test_display_never_allocates_or_evaluates_processing(self):
        machining=SUBJECT._frame_geometry._machining
        with mock.patch.object(SUBJECT._frame_geometry._process_adapter, 'frame_plan',
                               side_effect=AssertionError('Display executed machining')), \
             mock.patch.object(machining._load('assembly_geometry_process_runtime'), 'evaluate',
                               side_effect=AssertionError('Display evaluated a local process')), \
             mock.patch.object(machining, '_stock_record',
                               side_effect=AssertionError('Display allocated process stock')):
            for face in ('single', 'two', 'three', 'five'):
                for mode in ('segment_weld','plane_v_notch','spatial_v_notch'):
                    with self.subTest(face=face,mode=mode):
                        document = generate(face, mode, 'display', accessDoorEnabled=True)
                        self.assertNotIn('tubeDesigner.assemblyProcessSource', document['extensions'])
                        self.assertNotIn('tubeDesigner.assemblyGeometryProcesses', document['extensions'])
                        self.assertFalse(any('.export.groove.' in n['key'] for n in document['geometry']))

    def test_plane_rectangle_uses_actual_stock_yz_section_and_piercings(self):
        document = generate('three', 'plane_v_notch', frameWidth=40, frameDepth=25, accessDoorEnabled=True,
                            doorFrameJoinType='v_groove_90:tool_library',
                            doorLeafFrameJoinType='v_groove_90:tool_library')
        declarations = source(document)
        self.assertTrue(any('access_door.fixed_frame.' in p['id'] for p in declarations['stocks']))
        self.assertTrue(any('access_door.leaf.frame.' in p['id'] for p in declarations['stocks']))
        outer = [i for i in process_items(document).values()
                 if i['key'].startswith('outer_frame.') and '.plane.' in i['key']]
        self.assertTrue(all(i['properties']['tubeDesigner.assemblyProcessStockProfile']['width'] == 25 for i in outer))
        self.assertTrue(all(i['properties']['tubeDesigner.assemblyProcessStockProfile']['depth'] == 40 for i in outer))
        self.assertTrue(all(i['properties']['tubeDesigner.assemblyProcessSecondaryCutters']==[] for i in outer))
        stock_ids={i['properties']['tubeDesigner.assemblyProcessStockId'] for i in outer}
        apertures=[i for i in declarations['instances'] if i['templateId']=='tube-profile-aperture'
                   and i['targets']['stock'] in stock_ids]
        self.assertTrue(apertures)
        self.assertTrue(all(i['processInput']['parts']['stock']['section']['profile']['width']==25
                            and i['processInput']['parts']['stock']['section']['profile']['depth']==40
                            for i in apertures))
        applied=document['extensions']['tubeDesigner.assemblyGeometryProcesses']['instances']
        self.assertTrue({i['instanceId'] for i in apertures} <= {i['instanceId'] for i in applied})

    def test_spatial_left_and_right_preserve_material_stations_and_faces(self):
        for side in ('left', 'right'):
            document = generate('three', 'spatial_v_notch', sidePosition=side, accessDoorEnabled=False)
            declaration = source(document)
            folds = [i for i in declaration['instances'] if i['templateId'] == 'node-v-notch-integrated']
            self.assertEqual(8, len(folds))
            rotations = {i['processInput']['geometry']['rotation'] for i in folds}
            self.assertGreater(len(rotations), 1)
            self.assertEqual(sorted(i['processInput']['geometry']['station'] for i in folds),
                             [i['processInput']['geometry']['station'] for i in folds])
            planned = runtime.resolve_process_plan(declaration['stocks'], declaration['instances'])
            reversed_order = runtime.resolve_process_plan(declaration['stocks'], list(reversed(declaration['instances'])))
            self.assertEqual({f['instanceId']: f['cumulativeTransform'] for f in planned['forming']},
                             {f['instanceId']: f['cumulativeTransform'] for f in reversed_order['forming']})

    def test_compensation_is_allocated_once_by_the_host_not_by_each_function(self):
        binding = {'outerFrameGroove': {'ref': {'scope': 'system', 'id': 'v-notch-sharp'},
                   'parameters': {'angle': 90, 'bottomStrategy': 'rounded', 'roundRadius': 0.5,
                                  'bendCompensation': True, 'kFactor': 0.62, 'leaveBottom': 1}}}
        plain = source(generate('three', 'plane_v_notch', accessDoorEnabled=False))
        compensated = source(generate('three', 'plane_v_notch', accessDoorEnabled=False,
                                     tubeDesignerToolBindings=binding))
        wall = DEFAULTS['frameWallThickness']
        fold_count = sum(i['templateId'] == 'node-v-notch-integrated' for i in compensated['instances'])
        addition = sum(p['length'] for p in compensated['stocks']) - sum(p['length'] for p in plain['stocks'])
        self.assertAlmostEqual(fold_count * math.pi / 2 * 0.62 * wall, addition, places=5)
        plan = runtime.resolve_process_plan(compensated['stocks'], compensated['instances'])
        self.assertTrue(all(p['lengthAddition'] == 0 for p in plan['materialRequirements']))
        self.assertEqual(len(compensated['stocks']), len(plan['manufacturingParts']))

    def test_user_v_and_edge_resources_keep_their_actual_reference_and_geometry(self):
        values = {**deepcopy(DEFAULTS), 'faceType': 'three', 'frameManufacturingMode': 'plane_v_notch',
                  'accessDoorEnabled': False}
        for original in ('v-notch-sharp', 'edge-arc-groove'):
            with self.subTest(resource=original), tempfile.TemporaryDirectory() as directory:
                tool_id = 'user-' + original
                package = Path(directory) / tool_id
                shutil.copytree(TEMPLATES / 'mold' / original, package)
                manifest = json.loads((package / 'tool.json').read_text(encoding='utf-8'))
                manifest['id'] = tool_id
                (package / 'tool.json').write_text(json.dumps(manifest, ensure_ascii=False), encoding='utf-8')
                params = deepcopy(values)
                params['tubeDesignerToolBindings'] = {'outerFrameGroove': {
                    'ref': {'scope': 'user', 'id': tool_id}, 'parameters': {}}}
                before = deepcopy(params)
                document = expand_resource_model(SUBJECT.generate(params, {'template': DESCRIPTOR, 'geometryPurpose': 'manufacturing',
                                                       'userMouldRoot': directory}))
                self.assertEqual(before, params)
                self.assertEqual(before, document['parameters'])
                declaration = source(document)
                self.assertNotIn(directory, json.dumps(declaration))
                plan = runtime.resolve_process_plan(declaration['stocks'], declaration['instances'],
                                                    runtime_context={'userMouldRoot': directory})
                features = [o['requestFeature'] for o in plan['operations'] if 'requestFeature' in o]
                self.assertTrue(features)
                self.assertTrue(all(f['toolRef']['scope'] == 'user' for f in features))
                self.assertTrue(all(f['toolRef']['id'] == tool_id for f in features))
                self.assertTrue(any('.mould.' in n['key'] for n in document['geometry']))

    def test_root_datum_changes_only_material_and_nominal_target_meets_design(self):
        design = generate('three','spatial_v_notch','display',accessDoorEnabled=False)
        for bottom_reference,leave in (('outer',1),('outer',2),('inner',1)):
            bindings = {'outerFrameGroove': {'ref': {'scope':'system','id':'v-notch-sharp'},
                        'parameters': {'bottomReference':bottom_reference,'leaveBottom':leave}}}
            document = generate('three','spatial_v_notch',accessDoorEnabled=False,
                                tubeDesignerToolBindings=bindings)
            unaffected = generate('three','spatial_v_notch','display',accessDoorEnabled=False,
                                 tubeDesignerToolBindings=bindings)
            self.assertEqual(design['geometry'],unaffected['geometry'])
            self.assertEqual(design['items'],unaffected['items'])
            spatial = next(i for i in document['items'] if '.spatial.' in i['key'])
            path = spatial['properties']['tubeDesigner.frameManufacturing']
            self.assertEqual('pass',path['targetSpanCheck']['status'])
            self.assertLess(path['targetSpanCheck']['maximumDeviation'],1e-4)
            expected = DEFAULTS['frameWidth']/2-leave-(DEFAULTS['frameWallThickness'] if bottom_reference=='inner' else 0)
            self.assertTrue(all(abs(b['materialReserve']-expected)<1e-6 for b in path['bends']))

    def test_inactive_compensation_draft_does_not_allocate_material_or_block(self):
        binding = {'outerFrameGroove': {'ref': {'scope':'system','id':'v-notch-sharp'},
                   'parameters': {'bottomStrategy':'sharp','bendCompensation':True,'kFactor':-100}}}
        plain = generate('three','plane_v_notch',accessDoorEnabled=False)
        inactive = generate('three','plane_v_notch',accessDoorEnabled=False,tubeDesignerToolBindings=binding)
        self.assertEqual(plain['geometry'],inactive['geometry'])
        a,b=source(plain),source(inactive)
        self.assertEqual(a['stocks'],b['stocks'])

    def test_continuous_orthogonal_node_processes_c_without_recutting_ab(self):
        for joint in ('weld', 'insert', 'tabs'):
            with self.subTest(joint=joint):
                local = example_input('orthogonal-corner')
                original = deepcopy(local)
                result = runtime.evaluate_process('orthogonal-corner', local,
                    {'abJoint': 'continuous', 'cJoint': joint})
                self.assertTrue(result['applicable'], result['reason'])
                self.assertEqual(original, local)
                self.assertFalse(any(o.get('processId', '').startswith('ab-') for o in result['operations']))
                self.assertTrue(any(o['role'] == 'memberC' for o in result['operations']))
                self.assertTrue(all(r['lengthAddition'] == 0 for r in result['materialRequirements']))
                if joint == 'weld':
                    self.assertEqual([['memberA','memberC'],['memberB','memberC']],
                                     [c['roles'] for c in result['contactRequirements']])
                    self.assertTrue(all(c['minimumContactAreaMm2']>0 for c in result['contactRequirements']))

    def test_continuous_rectangle_uses_actual_near_wall_and_requires_material_contact(self):
        local = example_input('orthogonal-corner')
        for role,part in local['parts'].items():
            part['parameters'].update(width=25,depth=38,wallThickness=1.2,cornerRadius=0,innerRadius=0)
            if role != 'memberC':
                matrix = part['matrix']
                for row in range(3):
                    matrix[row*4+1],matrix[row*4+2] = matrix[row*4+2],-matrix[row*4+1]
        result = runtime.evaluate_process('orthogonal-corner',local,{'abJoint':'continuous','cJoint':'weld'})
        self.assertTrue(result['applicable'],result['reason'])
        self.assertEqual(19,result['operations'][0]['requestEnd']['trim'])
        self.assertEqual(2,len(result['contactRequirements']))
        for joint in ('insert','tabs'):
            result = runtime.evaluate_process('orthogonal-corner',local,{'abJoint':'continuous','cJoint':joint})
            self.assertFalse(result['applicable'])
            self.assertIn('C 端口须小于',result['reason'])
        displaced = deepcopy(local)
        displaced['parts']['memberC']['matrix'][3] += 1
        result = runtime.evaluate_process('orthogonal-corner',displaced,{'abJoint':'continuous','cJoint':'weld'})
        self.assertFalse(result['applicable'])
        self.assertIn('实际端点',result['reason'])
        no_contact = deepcopy(local)
        no_contact['parts']['memberC']['parameters'].update(width=600,depth=600)
        result = runtime.evaluate_process('orthogonal-corner',no_contact,{'abJoint':'continuous','cJoint':'weld'})
        self.assertFalse(result['applicable'])
        self.assertIn('正面积接触',result['reason'])
        incompatible = deepcopy(local)
        incompatible['parts']['memberB']['parameters']['depth']=40
        result = runtime.evaluate_process('orthogonal-corner',incompatible,{'abJoint':'continuous','cJoint':'weld'})
        self.assertFalse(result['applicable'])
        self.assertTrue(result['reason'])

    def test_same_c_post_two_ends_are_trimmed_from_one_original_stock(self):
        local = example_input('orthogonal-corner')
        c = local['parts']['memberC']
        c['id'], c['length'] = 'post', 1000
        stocks = []
        for role, part in local['parts'].items():
            part['id'] = 'post' if role == 'memberC' else role
            stocks.append({key: deepcopy(part[key]) for key in ('id', 'profileRef', 'parameters', 'length', 'matrix')})
        instances = []
        for end in ('start', 'end'):
            current = deepcopy(local)
            current['parts']['memberC']['anchor'] = {'kind': 'end', 'end': end}
            if end == 'end':
                for role in ('memberA','memberB'):
                    for at,axis in zip((3,7,11),(0,4,8)):
                        current['parts'][role]['matrix'][at] += c['matrix'][axis]*c['length']
            instances.append({'instanceId': 'c-' + end, 'templateId': 'orthogonal-corner',
                'processInput': current, 'parameters': {'abJoint': 'continuous', 'cJoint': 'weld'},
                'targets': {role: part['id'] for role, part in current['parts'].items()}})
        original = deepcopy((stocks, instances))
        plan = runtime.resolve_process_plan(stocks, instances)
        self.assertEqual(original, (stocks, instances))
        post = next(p for p in plan['manufacturingParts'] if p['stockId'] == 'post')['request']
        self.assertEqual(1000, post['length'])
        near = local['parts']['memberA']['parameters']['width'] / 2
        self.assertEqual([near, near], [post['ends'][end]['trim'] for end in ('start', 'end')])
        self.assertEqual(4,len(plan['contactRequirements']))
        repeated = runtime.resolve_process_plan(stocks,[*instances,deepcopy(instances[0])])
        self.assertEqual(plan['contactRequirements'],repeated['contactRequirements'])
        changed = runtime.resolve_process_plan(stocks, instances[:1])
        remaining = next(p for p in changed['manufacturingParts'] if p['stockId'] == 'post')['request']
        self.assertEqual('keep', remaining['ends']['end']['type'])

    def test_three_face_actual_source_connects_both_shared_posts_at_both_ends(self):
        for mode in ('plane_v_notch', 'spatial_v_notch'):
            with self.subTest(mode=mode):
                document = generate('three', mode, accessDoorEnabled=False)
                declaration = source(document)
                joints = [i for i in declaration['instances'] if i['templateId'] == 'orthogonal-corner']
                self.assertEqual(4, len(joints))
                post_ids = {i['targets']['memberC'] for i in joints}
                self.assertEqual(2, len(post_ids))
                self.assertEqual(len(declaration['stocks']), len({p['id'] for p in declaration['stocks']}))
                plan = runtime.resolve_process_plan(declaration['stocks'], declaration['instances'])
                self.assertEqual(8,len(plan['contactRequirements']))
                blanks = {p['stockId']: p['request'] for p in plan['manufacturingParts']}
                raw = {p['id']: p for p in declaration['stocks']}
                actual = process_items(document)
                for post_id in post_ids:
                    self.assertEqual(DEFAULTS['height']-DEFAULTS['frameWidth'], raw[post_id]['length'])
                    self.assertEqual(DEFAULTS['height']-2*DEFAULTS['frameWidth'], actual[post_id]['properties']['length'])
                    self.assertEqual([DEFAULTS['frameWidth']/2]*2,
                                     [blanks[post_id]['ends'][end]['trim'] for end in ('start', 'end')])
                for instance in joints:
                    self.assertEqual('continuous', instance['parameters']['abJoint'])
                    evaluated = runtime.evaluate_process(instance['templateId'], instance['processInput'], instance['parameters'])
                    self.assertTrue(evaluated['applicable'], evaluated['reason'])
                    self.assertEqual({'memberC'}, {o['role'] for o in evaluated['operations']})


if __name__ == '__main__':
    unittest.main()
