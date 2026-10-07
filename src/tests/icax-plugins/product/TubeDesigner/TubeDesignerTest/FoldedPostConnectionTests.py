"""Real U-frame post choices, stock extents and downstream dependency replay."""
from copy import deepcopy
import json
import unittest

from WindowManufacturingInputTests import package, load, execute, ROOT
from icax_template_sdk.manufacturing import compose_manufacturing_model
from DependencyWindowTests import manufactured_geometry


def window(joint='weld', **changes):
    _, defaults, core = package('single_face_security_window')
    values = {**deepcopy(defaults), 'faceType': 'three', 'frameManufacturingMode': 'plane_v_notch',
              'height': 1000, 'width': 1200, 'accessDoorEnabled': False,
              'verticalMaximumCenterSpacing': 600, 'sideVerticalMaximumCenterSpacing': 600,
              'topBottomRodMaximumCenterSpacing': 600, 'foldedPostJoint': joint, **changes}
    if joint != 'weld' and 'outerFramePostMaterial' not in changes:
        # Prepare the physical product materials explicitly before machining.
        values['outerFramePostMaterial'] = ('middle_only' if values['faceType'] in ('two', 'three')
            and values['frameManufacturingMode'] != 'segment_weld' else 'independent')
    before = deepcopy(values)
    declaration, design = core.manufacturing(values), core.display(values)
    compiled = load('assembly_window_process').compile_window_assembly(
        compose_manufacturing_model(declaration, design))
    assert values == before
    return values, declaration, design, compiled


def allocate(compiled):
    before = deepcopy(compiled)
    result = load('assembly_material_allocation').allocate_window_manufacturing(compiled)
    assert compiled == before
    return result


def full_plan(allocation):
    # Replay the complete real recipe so a smaller post cannot hide a new
    # infill collision behind successful isolated corner-node checks.
    source = allocation['assemblySource']
    return load('assembly_template_runtime').resolve_process_plan(source['stocks'], source['instances'])


class FoldedPostConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = {}
        for joint in ('weld', 'insert', 'tabs'):
            inputs = window(joint)
            cls.cases[joint] = (*inputs, allocate(inputs[-1]))

    def test_active_connections_use_actual_post_sections_and_two_end_allowances(self):
        for joint, (values, declaration, design, compiled, allocation) in self.cases.items():
            with self.subTest(joint=joint):
                posts = {item['key']: item for item in allocation['model']['items']
                         if item['key'].startswith('outer_frame.vertical.')}
                self.assertEqual(len(posts), 4)
                for index in (1, 4):
                    post = posts[f'outer_frame.vertical.{index:04d}']['properties']
                    self.assertEqual(post['length'], 1000)
                    self.assertEqual(post['tubeDesigner.profile']['width'], 38)
                for index in (2, 3):
                    post = posts[f'outer_frame.vertical.{index:04d}']['properties']
                    allowance = 0 if joint == 'weld' else 12
                    self.assertAlmostEqual(post['length'], 1000 - 2 * 38 + 2 * allowance)
                    self.assertEqual(post['tubeDesigner.profile']['width'], 38 if joint == 'weld' else 30)
                calls = [call for call in allocation['assemblySource']['instances']
                         if call['templateId'] == 'orthogonal-corner']
                self.assertEqual(len(calls), 4)
                descriptor = json.loads((ROOT.parent / 'assembly/orthogonal-corner/assembly.json').read_text('utf-8'))
                defaults = {item['key']: item['defaultValue'] for item in descriptor['parameters']
                            if item.get('scope') != 'scene'}
                for call in calls:
                    self.assertEqual(call['parameters']['abJoint'], 'continuous')
                    self.assertEqual(call['parameters']['cJoint'], joint)
                    normalized = {**defaults, **deepcopy(call['parameters'])}
                    original = deepcopy(normalized)
                    evaluated = load('assembly_template_runtime').evaluate_process(
                        call['templateId'], call['processInput'], normalized, call['processDrafts'])
                    self.assertTrue(evaluated['applicable'], evaluated['reason'])
                    self.assertEqual(normalized, original)
                    self.assertEqual(evaluated['parameters'], normalized)
                # Evaluate the actual selected end and receiver tools, including
                # both real endpoint anchors, rather than checking labels only.
                self.assertTrue(full_plan(allocation)['operations'])
                apertures = [call for call in allocation['assemblySource']['instances']
                             if call['templateId'] == 'tube-profile-aperture']
                self.assertTrue(apertures)
                self.assertTrue(all(call['parameters']['checkFit'] for call in apertures))

    def test_connections_wait_for_only_their_frame_and_post_apertures_wait_for_end_allocation(self):
        for joint, (_, _, _, compiled, allocation) in self.cases.items():
            with self.subTest(joint=joint):
                graph = allocation['stagePlan']['dependencies']
                calls = {call['key']: call for call in compiled['processes']}
                folds = {call['key'] for call in calls.values()
                         if call['definition']['templateId'] == 'node-v-notch-integrated'}
                self.assertEqual(len(folds), 4)
                self.assertTrue(all(not set(graph[key]) & folds for key in folds))
                for call in calls.values():
                    definition = call['definition']
                    if definition['templateId'] == 'orthogonal-corner':
                        stock = definition['targets']['memberA']
                        owned_folds = {key for key in folds if calls[key]['definition']['targets']['stock'] == stock}
                        self.assertEqual(set(graph[call['key']]) & folds, owned_folds)
                    elif definition['templateId'] == 'tube-profile-aperture':
                        related = {definition['targets']['stock'],
                                   definition['processInput']['parts']['branch']['itemKey']}
                        needed = {key for key, predecessor in calls.items()
                                  if predecessor['definition']['templateId'] == 'orthogonal-corner'
                                  and predecessor['definition']['targets']['memberC'] in related}
                        self.assertTrue(needed <= set(graph[call['key']]))
                positions = {key: i for i, key in enumerate(allocation['stagePlan']['orderedKeys'])}
                self.assertTrue(all(positions[dep] < positions[key] for key, deps in graph.items() for dep in deps))

    def test_reversed_submission_preserves_real_post_and_receiver_tool_requests(self):
        for joint in ('insert', 'tabs'):
            with self.subTest(joint=joint):
                compiled, reference = self.cases[joint][-2:]
                reverse = deepcopy(compiled)
                reverse['processes'].reverse()
                actual = allocate(reverse)
                def material(plan):
                    result = deepcopy(plan)
                    result['initialMaterialAllocations'].sort(key=lambda item: (item['stockId'], item['end']))
                    return result
                self.assertEqual(material(actual['materialPlan']), material(reference['materialPlan']))
                self.assertEqual(actual['stagePlan']['dependencies'], reference['stagePlan']['dependencies'])
                expected_plan, actual_plan = full_plan(reference), full_plan(actual)
                def requests(plan):
                    result = {}
                    for source in plan['manufacturingParts']:
                        part = deepcopy(source)
                        part['operationIds'].sort()
                        part['sourceMappings'].sort(key=lambda mapping: json.dumps(mapping, sort_keys=True))
                        part['request']['features'].sort(key=lambda feature: feature['id'])
                        if 'neutralOperations' in part['request']:
                            part['request']['neutralOperations'].sort(key=lambda operation: operation['operationId'])
                        result[part['stockId']] = part
                    return result
                self.assertEqual(requests(actual_plan), requests(expected_plan))
                self.assertEqual({op['operationId']: op for op in actual_plan['operations']},
                                 {op['operationId']: op for op in expected_plan['operations']})

    def test_public_worker_freezes_the_declared_graph_and_preserves_input(self):
        values, declaration, design, compiled, allocation = self.cases['tabs']
        original = deepcopy((values, declaration, design, compiled))
        result = execute(declaration, design)
        self.assertEqual((values, declaration, design, compiled), original)
        self.assertEqual(result['extensions']['tubeDesigner.assemblyStagePlan'], allocation['stagePlan'])
        self.assertEqual(result['extensions']['tubeDesigner.assemblyProcessSource'], allocation['assemblySource'])
        from icax_template_sdk import expand_resource_model
        actual = expand_resource_model(result)
        roots, reachable = manufactured_geometry(actual)
        self.assertTrue(roots and reachable)

    def test_incomplete_calls_and_impossible_insert_are_rejected(self):
        _, declaration, design, _, _ = self.cases['weld']
        incomplete = deepcopy(declaration)
        for key in load('security_window_post_connections').POST_DEFAULTS:
            del incomplete['processes'][0]['definition']['parameters'][key]
        with self.assertRaisesRegex(ValueError, 'explicit local choices'):
            load('assembly_window_process').compile_window_assembly(compose_manufacturing_model(incomplete, design))
        for changes in ({'foldedPostWidth': 38, 'foldedPostDepth': 38}, {'foldedPostInsertDepth': .1}):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, '端口|内孔|近侧|插入|穿过'):
                impossible = window('insert', **changes)[-1]
                invalid = allocate(impossible)
                full_plan(invalid)


if __name__ == '__main__':
    unittest.main()
