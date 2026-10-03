"""Single-face window regression for frozen material planning and local cuts.

These tests execute the Python providers and compare their exact cutter graphs,
stock requests and source mappings. Native solid evaluation is covered by the
product-preview acceptance executable; no sampled outline stands in for it.
"""
from copy import deepcopy
import math
import unittest

from WindowManufacturingInputTests import execute, load, package, forbid_product_callbacks
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model


FRAME = 'outer_frame.continuous.0001'
FOLD = 'node-v-notch-integrated'


def compiled_window(enabled=True, factor=.35, male_head=False):
    descriptor, defaults, core = package('single_face_security_window')
    values = {**deepcopy(defaults), 'faceType': 'single', 'accessDoorEnabled': False,
        'frameManufacturingMode': 'plane_v_notch', 'horizontalMaximumCenterSpacing': 1000,
        'verticalMaximumCenterSpacing': 600, 'firstHorizontalTopOffset': 200,
        'lastHorizontalBottomOffset': 200,
        'tubeDesignerToolBindings': {'outerFrameGroove': {
            'ref': {'scope': 'system', 'id': 'v-notch-sharp'},
            'parameters': {'bottomStrategy': 'rounded', 'roundRadius': .5,
                'bendCompensation': enabled, 'kFactor': factor, 'leaveBottom': 1}}}}
    original = deepcopy(values)
    design, declaration = core.display(values), core.manufacturing(values)
    compiled = load('assembly_window_process').compile_window_assembly(
        compose_manufacturing_model(declaration, design))
    assert values == original
    if male_head:
        # Additional local-provider fixture, not a new window option. The
        # existing end-insertion calls and receiving apertures remain active.
        member = next(item for item in compiled['items']
                      if item['key'].startswith('main_grid.horizontal.'))
        vertices = member['manufacturingInput']['path']['vertices']
        left, right = vertices[0]['point'][0], vertices[-1]['point'][0]
        center = vertices[0]['point'][2]
        profile = member['properties']['tubeDesigner.profile']
        compiled['processes'].append({'key': member['key'] + '.fixture.male-head', 'kind': 'assembly-process',
            'definition': {'templateId': 'structural-male-head',
                'processInput': {'schema': 'icax.assembly-process-input', 'schemaVersion': 2,
                    'parts': {'stock': {'scope': 'manufacturing', 'itemKey': member['key'],
                                       'state': 'initial'}},
                    'geometry': {'visibleLeft': left + 5, 'visibleRight': right - 5,
                        'centerZ': center, 'profileWidth': profile['width'],
                        'profileDepth': profile['depth'], 'wall': profile['wallThickness']}},
                'parameters': {'length': 5, 'width': 8, 'corner': 'round', 'size': 2},
                'processDrafts': {}, 'targets': {'stock': member['key']}, 'dependencies': []}})
    validate_manufacturing_model(compiled)
    return compiled, values, descriptor, core


def allocate_compiled(compiled):
    original = deepcopy(compiled)
    with forbid_product_callbacks():
        result = load('assembly_material_allocation').allocate_window_manufacturing(compiled)
    assert compiled == original
    return result


def resolve_allocation(allocation):
    source = allocation['assemblySource']
    original = deepcopy(source)
    runtime = load('assembly_template_runtime')
    with forbid_product_callbacks():
        plan = runtime.resolve_process_plan(source['stocks'], source['instances'])
    assert source == original
    for instance in source['instances']:
        original_instance = deepcopy(instance)
        evaluated = runtime.evaluate_process(instance['templateId'], instance['processInput'],
            instance['parameters'], instance['processDrafts'])
        assert evaluated['applicable'], (instance['instanceId'], evaluated['reason'])
        assert evaluated['parameters'] == instance['parameters']
        assert instance == original_instance
    return plan


def material_stock(allocation, key=FRAME):
    return next(stock for stock in allocation['materialPlan']['stocks'] if stock['stockId'] == key)


def stock_length(allocation, key=FRAME):
    return next(stock['length'] for stock in allocation['assemblySource']['stocks'] if stock['id'] == key)


def semantic_snapshot(allocation, plan):
    """Compare generated geometry and placement, allowing unordered cut sets."""
    requests = {}
    for part in plan['manufacturingParts']:
        request = deepcopy(part['request'])
        request['features'] = sorted(request['features'], key=lambda item: item['id'])
        request['neutralOperations'] = sorted(request.get('neutralOperations', []),
                                               key=lambda item: item['operationId'])
        requests[part['stockId']] = {'request': request, 'matrix': part['matrix']}
    model = allocation['model']
    return {
        'requests': requests,
        'stockGeometry': {node['key']: node for node in model['geometry']},
        'items': {item['key']: item for item in model['items']},
        'mappings': {stock['stockId']: stock['sourceMembers']
                     for stock in allocation['materialPlan']['stocks']},
        'bends': {fold['instanceId']: fold for fold in plan['forming']},
        'cutOperations': {operation['operationId']: operation for operation in plan['operations']},
        'instances': {instance['instanceId']: instance for instance in plan['instances']}}


class AssemblyProcessStageWindowTests(unittest.TestCase):
    def test_material_and_stage_plans_are_frozen_with_the_actual_model(self):
        compiled, values, _, core = compiled_window()
        allocation = allocate_compiled(compiled)
        plan = resolve_allocation(allocation)
        material = allocation['materialPlan']
        self.assertEqual(material['schema'], 'icax.assembly-material-plan')
        self.assertEqual(material['schemaVersion'], 1)
        self.assertEqual(allocation['model']['extensions']['tubeDesigner.assemblyMaterialPlan'], material)
        self.assertEqual(allocation['model']['extensions']['tubeDesigner.assemblyStagePlan'], allocation['stagePlan'])
        stock = material_stock(allocation)
        self.assertAlmostEqual(stock['length'], stock_length(allocation), places=9)
        self.assertEqual(len(stock['bends']), 4)
        props = next(item['properties'] for item in allocation['model']['items'] if item['key'] == FRAME)
        self.assertEqual(stock['sourceMembers'], props['manufacturing.sourceMembers'])
        self.assertEqual(sorted(bend['station'] for bend in stock['bends']),
                         [bend['station'] for bend in stock['bends']])
        self.assertTrue(all(requirement['lengthAddition'] == 0
                            for requirement in plan['materialRequirements']))
        self.assertEqual(set(allocation['stagePlan']['materializedProcessKeys']),
                         {call['instanceId'] for call in allocation['assemblySource']['instances']})
        with forbid_product_callbacks():
            generated = load('assembly_manufacturing_executor').execute_manufacturing(compiled)
        self.assertEqual(generated['extensions']['tubeDesigner.assemblyMaterialPlan'], material)
        self.assertEqual(generated['extensions']['tubeDesigner.assemblyStagePlan'], allocation['stagePlan'])
        declaration, design = core.manufacturing(values), core.display(values)
        original = deepcopy((declaration, design, values))
        public_result = execute(declaration, design)
        self.assertEqual((declaration, design, values), original)
        self.assertEqual(public_result['extensions']['tubeDesigner.assemblyMaterialPlan'], material)
        self.assertEqual(public_result['extensions']['tubeDesigner.assemblyStagePlan'], allocation['stagePlan'])

    def test_k_switch_and_changed_k_allocate_each_corner_exactly_once(self):
        disabled = allocate_compiled(compiled_window(False, .35)[0])
        low = allocate_compiled(compiled_window(True, .2)[0])
        high = allocate_compiled(compiled_window(True, .6)[0])
        wall = compiled_window()[1]['frameWallThickness']
        for allocation, factor in ((disabled, 0), (low, .2), (high, .6)):
            with self.subTest(factor=factor):
                bends = material_stock(allocation)['bends']
                self.assertEqual(len(bends), 4)
                for bend in bends:
                    self.assertAlmostEqual(bend['lengthAddition'], math.pi / 2 * factor * wall, places=9)
                self.assertAlmostEqual(stock_length(allocation) - stock_length(disabled),
                    sum(bend['lengthAddition'] for bend in bends), places=9)
                resolve_allocation(allocation)
        self.assertNotEqual(material_stock(low)['sourceMembers'], material_stock(high)['sourceMembers'])
        low_apertures = {call['instanceId']: call['processInput']['parts']['branch']['matrix']
                        for call in low['assemblySource']['instances']
                        if call['templateId'] == 'tube-profile-aperture' and call['targets']['stock'] == FRAME}
        high_apertures = {call['instanceId']: call['processInput']['parts']['branch']['matrix']
                         for call in high['assemblySource']['instances']
                         if call['templateId'] == 'tube-profile-aperture' and call['targets']['stock'] == FRAME}
        self.assertEqual(set(low_apertures), set(high_apertures))
        self.assertNotEqual(low_apertures, high_apertures, 'holes must use the final unfolded mapping')

    def test_hidden_k_draft_does_not_change_material_or_mapping(self):
        low = allocate_compiled(compiled_window(False, .2)[0])
        high = allocate_compiled(compiled_window(False, .8)[0])
        self.assertEqual(low['materialPlan'], high['materialPlan'])
        self.assertEqual(low['model']['geometry'], high['model']['geometry'])
        self.assertEqual(low['assemblySource']['stocks'], high['assemblySource']['stocks'])
        self.assertEqual({call['instanceId']: call['processInput']
                          for call in low['assemblySource']['instances']},
                         {call['instanceId']: call['processInput']
                          for call in high['assemblySource']['instances']})

    def test_mixed_per_corner_k_and_submission_order_preserve_geometry_and_positions(self):
        compiled, values, _, _ = compiled_window(male_head=True)
        factors = (.1, .3, .5, .7)
        folds = [call for call in compiled['processes'] if call['definition']['templateId'] == FOLD]
        self.assertEqual(len(folds), len(factors))
        for call, factor in zip(folds, factors):
            call['definition']['processDrafts']['node-slot']['v-notch-sharp']['kFactor'] = factor
        reference = allocate_compiled(compiled)
        plan = resolve_allocation(reference)
        snapshot = semantic_snapshot(reference, plan)
        bends = material_stock(reference)['bends']
        expected = {call['key']: math.pi / 2 * factor * values['frameWallThickness']
                    for call, factor in zip(folds, factors)}
        self.assertEqual(set(expected), {bend['instanceId'] for bend in bends})
        for bend in bends:
            self.assertAlmostEqual(bend['lengthAddition'], expected[bend['instanceId']], places=9)
        operation_kinds = {operation['kind'] for operation in plan['operations']}
        self.assertIn('neutral-csg', operation_kinds)
        male = next(operation for operation in plan['operations']
                    if operation['instanceId'].endswith('.fixture.male-head'))
        self.assertEqual(male['operation'], 'intersect')
        self.assertTrue(any(call['templateId'] == 'tube-end-joint'
                            and call['processInput']['geometry']['mode'] == 'insert'
                            for call in reference['assemblySource']['instances']))
        self.assertTrue(any(operation.get('operation') == 'subtract' for operation in plan['operations']))
        phases = {stage['stage']: stage['processKeys'] for stage in reference['stagePlan']['stages']}
        self.assertEqual(set(phases['bend-unfolding']), {call['key'] for call in folds})
        self.assertIn(male['instanceId'], phases['connection-machining'])
        for order in ('all-reversed', 'folds-reversed', 'cuts-first'):
            with self.subTest(order=order):
                reordered = deepcopy(compiled)
                calls = reordered['processes']
                if order == 'all-reversed':
                    reordered['processes'] = list(reversed(calls))
                elif order == 'folds-reversed':
                    reversed_folds = iter(reversed([call for call in calls if call['definition']['templateId'] == FOLD]))
                    reordered['processes'] = [next(reversed_folds) if call['definition']['templateId'] == FOLD else call
                                              for call in calls]
                else:
                    reordered['processes'] = ([call for call in calls if call['definition']['templateId'] != FOLD]
                        + [call for call in calls if call['definition']['templateId'] == FOLD])
                result = allocate_compiled(reordered)
                self.assertEqual(semantic_snapshot(result, resolve_allocation(result)), snapshot)

    def test_each_real_path_corner_requires_exactly_one_fold(self):
        original = compiled_window()[0]
        first = next(call for call in original['processes'] if call['definition']['templateId'] == FOLD)
        for fault in ('missing', 'duplicate'):
            with self.subTest(fault=fault):
                compiled = deepcopy(original)
                if fault == 'missing':
                    compiled['processes'] = [call for call in compiled['processes'] if call['key'] != first['key']]
                    # Keep a valid declaration graph to exercise the geometric
                    # missing-corner guard rather than an unknown predecessor.
                    for call in compiled['processes']:
                        call['definition']['dependencies'] = [key for key in call['definition']['dependencies']
                                                               if key != first['key']]
                else:
                    duplicated = deepcopy(first)
                    duplicated['key'] += '.duplicate'
                    compiled['processes'].append(duplicated)
                before = deepcopy(compiled)
                with self.assertRaisesRegex(ValueError, '转角|折弯'):
                    allocate_compiled(compiled)
                self.assertEqual(compiled, before)

    def test_path_allocation_rejects_after_snapshots_instead_of_replacing_them(self):
        compiled = compiled_window()[0]
        fold = next(call for call in compiled['processes'] if call['definition']['templateId'] == FOLD)
        aperture = next(call for call in compiled['processes']
                        if call['definition']['templateId'] == 'tube-profile-aperture'
                        and call['definition']['targets']['stock'] == FRAME)
        aperture['definition']['processInput']['parts']['stock']['state'] = {'after': fold['key']}
        aperture['definition']['dependencies'] = [fold['key']]
        # This is a legal dependency on a writer of the same object. The
        # intrinsic executor supports it; allocation must reject it explicitly.
        load('assembly_process_stages').plan_stages(compiled['processes'])
        before = deepcopy(compiled)
        with self.assertRaisesRegex(ValueError, 'initial|快照|状态'):
            allocate_compiled(compiled)
        self.assertEqual(compiled, before)

    def test_k_enabled_does_not_hide_wrong_corner_angle_or_anchor(self):
        for fault in ('angle', 'negative-angle', 'vertex', 'outgoing-segment', 'axis-direction'):
            with self.subTest(fault=fault):
                compiled = compiled_window(True, .35)[0]
                fold = next(call for call in compiled['processes'] if call['definition']['templateId'] == FOLD)
                geometry = fold['definition']['processInput']['geometry']
                if fault == 'angle':
                    geometry['angle'] = 45
                elif fault == 'negative-angle':
                    geometry['angle'] = -90
                elif fault == 'vertex':
                    geometry['anchor']['vertexKey'] = 'vertex.3'
                elif fault == 'outgoing-segment':
                    geometry['anchor']['outgoingSegment'] = 'segment.3'
                else:
                    geometry['axisDirection'] = [1, 0, 0]
                before = deepcopy(compiled)
                with self.assertRaisesRegex(ValueError, '转角|折弯|相邻|角度'):
                    allocate_compiled(compiled)
                self.assertEqual(compiled, before)


if __name__ == '__main__':
    unittest.main()
