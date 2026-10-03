"""Spatial stock poses, independent post connections and fold material mapping."""
from copy import deepcopy
import math
import unittest

from FoldedPostConnectionTests import window, allocate, full_plan
from WindowManufacturingInputTests import execute
from AssemblyProcessStageWindowTests import semantic_snapshot


FRAME = 'outer_frame.spatial.continuous'


class SpatialFoldedPostConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = {}
        for face in ('two', 'three'):
            for joint in ('weld', 'insert', 'tabs'):
                inputs = window(joint, faceType=face, frameManufacturingMode='spatial_v_notch', sideWidth=600)
                cls.cases[(face, joint)] = (*inputs, allocate(inputs[-1]))

    def test_only_corner_posts_remain_independent_and_receive_both_end_connections(self):
        for (face, joint), (values, _, design, _, allocation) in self.cases.items():
            with self.subTest(face=face, joint=joint):
                count = 1 if face == 'two' else 2
                posts = [item for item in allocation['model']['items']
                         if item['key'].startswith('outer_frame.vertical.')]
                self.assertEqual(len(posts), count)
                source = allocation['assemblySource']
                self.assertEqual(sum(stock['id'] == FRAME for stock in source['stocks']), 1)
                self.assertEqual(sum(stock['id'].startswith('outer_frame.') for stock in source['stocks']), count + 1)
                frame = next(item for item in allocation['model']['items'] if item['key'] == FRAME)
                self.assertEqual(frame['properties']['tubeDesigner.profile']['width'], 38.)
                self.assertEqual(frame['properties']['tubeDesigner.profile']['depth'], 38.)
                design_posts = sorted(
                    (item for item in design['items'] if item['key'].startswith('outer_frame.vertical.')),
                    key=lambda item: item['key'])
                self.assertEqual(design_posts[0]['properties']['tubeDesigner.profile']['width'], 38.)
                self.assertEqual(design_posts[-1]['properties']['tubeDesigner.profile']['width'], 38.)
                for post in posts:
                    props = post['properties']
                    self.assertEqual(props['tubeDesigner.profile']['width'], 38. if joint == 'weld' else 30.)
                    self.assertAlmostEqual(props['length'], 924. if joint == 'weld' else 948.)
                    calls = [call for call in source['instances'] if call['templateId'] == 'orthogonal-corner'
                             and call['targets']['memberC'] == post['key']]
                    self.assertEqual(len(calls), 2)
                    self.assertEqual({call['processInput']['parts']['memberC']['anchor']['end'] for call in calls},
                                     {'start', 'end'})
                    self.assertTrue(all(call['parameters']['cJoint'] == joint for call in calls))
                    self.assertTrue(all(call['targets']['memberA']['stockId'] == FRAME
                                        and call['targets']['memberB']['stockId'] == FRAME for call in calls))

    def test_all_local_tools_resolve_after_all_frame_folds_without_ordering_the_folds(self):
        for (face, joint), (_, _, _, compiled, allocation) in self.cases.items():
            if joint == 'weld':
                continue
            with self.subTest(face=face, joint=joint):
                graph = allocation['stagePlan']['dependencies']
                processes = {call['key']: call for call in compiled['processes']}
                folds = {key for key, call in processes.items()
                         if call['definition']['templateId'] == 'node-v-notch-integrated'}
                self.assertEqual(len(folds), 6 if face == 'two' else 8)
                self.assertTrue(all(not set(graph[key]) & folds for key in folds))
                for key, call in processes.items():
                    definition = call['definition']
                    if definition['templateId'] == 'orthogonal-corner':
                        self.assertEqual(set(graph[key]) & folds, folds)
                    elif definition['templateId'] == 'tube-profile-aperture':
                        related = {definition['targets']['stock'],
                                   definition['processInput']['parts']['branch']['itemKey']}
                        ends = {other_key for other_key, other in processes.items()
                                if other['definition']['templateId'] == 'orthogonal-corner'
                                and other['definition']['targets']['memberC'] in related}
                        self.assertTrue(ends <= set(graph[key]))
                plan = full_plan(allocation)
                actual_ids = {op['instanceId'] for op in plan['operations']}
                corner_ids = {call['instanceId'] for call in allocation['assemblySource']['instances']
                              if call['templateId'] == 'orthogonal-corner'}
                self.assertTrue(corner_ids <= actual_ids)
                self.assertTrue(all(call['parameters']['checkFit'] for call in allocation['assemblySource']['instances']
                                    if call['templateId'] == 'tube-profile-aperture'))

    def test_public_worker_preserves_frozen_source_and_parameter_drafts(self):
        for face in ('two', 'three'):
            for joint in ('insert', 'tabs'):
                with self.subTest(face=face, joint=joint):
                    values, declaration, design, compiled, allocation = self.cases[(face, joint)]
                    before = deepcopy((values, declaration, design, compiled))
                    result = execute(declaration, design)
                    self.assertEqual((values, declaration, design, compiled), before)
                    self.assertEqual(result['extensions']['tubeDesigner.assemblyProcessSource'], allocation['assemblySource'])
                    self.assertEqual(result['extensions']['tubeDesigner.assemblyStagePlan'], allocation['stagePlan'])

    def test_hidden_single_face_planner_draft_cannot_change_spatial_sections_or_connections(self):
        for face in ('two', 'three'):
            for joint in ('insert', 'tabs'):
                with self.subTest(face=face, joint=joint):
                    _, _, expected_design, _, expected = self.cases[(face, joint)]
                    values, declaration, design, compiled = window(
                        joint, faceType=face, frameManufacturingMode='spatial_v_notch', sideWidth=600,
                        assemblyPlanningMode='external_templates')
                    self.assertEqual(values['assemblyPlanningMode'], 'external_templates')
                    self.assertEqual(declaration['processes'][0]['definition']['parameters']['assemblyPlanningMode'],
                                     'builtin_rules')
                    self.assertEqual(design, expected_design)
                    actual = allocate(compiled)
                    self.assertEqual(actual['assemblySource'], expected['assemblySource'])
                    self.assertEqual(actual['materialPlan'], expected['materialPlan'])
                    self.assertTrue(full_plan(actual)['operations'])

    def test_spatial_k_allowance_updates_stock_and_receivers_and_submission_order_is_irrelevant(self):
        face, joint = 'three', 'tabs'
        reference = self.cases[(face, joint)][-1]
        low_high = []
        for factor in (.2, .6):
            compiled = window(joint, faceType=face, frameManufacturingMode='spatial_v_notch',
                              tubeDesignerToolBindings={'outerFrameGroove': {
                                  'ref': {'scope': 'system', 'id': 'v-notch-sharp'},
                                  'parameters': {'bottomStrategy': 'rounded', 'roundRadius': .5,
                                      'bendCompensation': True, 'kFactor': factor, 'leaveBottom': 1}}})[-1]
            allocation = allocate(compiled)
            stock = next(stock for stock in allocation['materialPlan']['stocks'] if stock['stockId'] == FRAME)
            baseline = next(stock for stock in reference['materialPlan']['stocks'] if stock['stockId'] == FRAME)
            self.assertEqual(len(stock['bends']), 8)
            for bend in stock['bends']:
                self.assertAlmostEqual(bend['lengthAddition'], math.pi / 2 * factor * 1.2, places=9)
            self.assertAlmostEqual(stock['length'] - baseline['length'], 8 * math.pi / 2 * factor * 1.2, places=9)
            plan = full_plan(allocation)
            reverse = deepcopy(compiled)
            reverse['processes'].reverse()
            reversed_allocation = allocate(reverse)
            self.assertEqual(semantic_snapshot(allocation, plan),
                             semantic_snapshot(reversed_allocation, full_plan(reversed_allocation)))
            low_high.append({call['instanceId']: {'targets': call['targets'], 'parts': call['processInput']['parts']}
                             for call in allocation['assemblySource']['instances']
                             if call['templateId'] == 'orthogonal-corner'})
        self.assertEqual(set(low_high[0]), set(low_high[1]))
        self.assertEqual({key: value['parts'] for key, value in low_high[0].items()},
                         {key: value['parts'] for key, value in low_high[1].items()},
                         'K allowance must preserve the installed source member poses')
        starts = lambda cases: {(key, role): value['targets'][role]['start']
                               for key, value in cases.items() for role in ('memberA', 'memberB')}
        low, high = starts(low_high[0]), starts(low_high[1])
        self.assertNotEqual(low, high, 'receiver stock stations must use the completed unfolded material map')
        delta = math.pi / 2 * (.6 - .2) * 1.2
        for key in low:
            preceding_bends = (high[key] - low[key]) / delta
            self.assertGreaterEqual(preceding_bends, -1.e-8)
            self.assertLessEqual(preceding_bends, 8.)
            self.assertAlmostEqual(preceding_bends, round(preceding_bends), places=8)


if __name__ == '__main__':
    unittest.main()
