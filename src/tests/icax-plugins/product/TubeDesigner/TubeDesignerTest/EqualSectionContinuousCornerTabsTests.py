"""Current folded frame tabs fit their retained skins, not the whole port."""
from copy import deepcopy
import json
import math
import unittest
from unittest.mock import patch

from WindowManufacturingInputTests import package, load, ROOT
from FoldedPostConnectionTests import allocate, full_plan
from GridEndConnectionTests import window as grid_window
from AssemblyOrthogonalCornerTests import participants, section
from AssemblyContinuousCornerInsertionTests import turned_input
from icax_template_sdk.manufacturing import compose_manufacturing_model


class EqualSectionContinuousCornerTabsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.product = package('single_face_security_window')
        cls.runtime = load('assembly_template_runtime')
        cls.punch = load('punch_tool_runtime')
        cls.cases = []
        for face, side in (('two', 'right'), ('two', 'left'), ('three', 'right')):
            values = {**deepcopy(cls.defaults), 'faceType': face, 'sidePosition': side,
                'width': 1200., 'sideWidth': 600., 'height': 1800., 'accessDoorEnabled': True,
                'frameManufacturingMode': 'plane_v_notch', 'foldedPostJoint': 'tabs',
                'outerFrameGrooveTool': 'system:edge-arc-groove',
                'doorFrameJoinType': 'miter_45', 'doorLeafFrameJoinType': 'miter_45',
                'horizontalEndConnection': 'insert', 'verticalEndConnection': 'insert',
                'assemblyClearance': .1}
            original = deepcopy(values)
            design, declaration = cls.product.display(values), cls.product.manufacturing(values)
            compiled = load('assembly_window_process').compile_window_assembly(
                compose_manufacturing_model(declaration, design))
            original_compiled = deepcopy(compiled)
            allocation = allocate(compiled)
            original_allocation = deepcopy(allocation)
            plan = full_plan(allocation)
            assert values == original and compiled == original_compiled and allocation == original_allocation
            cls.cases.append((values, design, allocation, plan))

    def corner(self):
        return deepcopy(next(call for call in self.cases[0][2]['assemblySource']['instances']
                             if call['templateId'] == 'orthogonal-corner'))

    def evaluate(self, call, **changes):
        before = deepcopy(call)
        descriptor = self.runtime._template_by_id('orthogonal-corner')
        values = self.runtime._validated_values(descriptor, {**call['parameters'], **changes})
        result = self.runtime.evaluate_process('orthogonal-corner', call['processInput'], values)
        self.assertEqual(call, before)
        self.assertEqual(result['parameters'], values)
        return result

    def prepared_corner_features(self, allocation, plan):
        profiles = {item['key']: item['properties']['tubeDesigner.profile']
                    for item in allocation['model']['items']}
        for material in plan['manufacturingParts']:
            features = [deepcopy(feature) for feature in material['request']['features']
                        if feature['toolRef']['id'] == 'paired-side-slots'
                        and '.post:' in feature['id']]
            if not features:
                continue
            host = profiles[material['stockId']]
            for feature in features:
                feature['section']['profile'] = deepcopy(profiles[feature['section']['profileRef']['id']])
            request = {'targetSection': host,
                'bounds': {'min': [0., -19., -19.],
                           'max': [material['request']['length'], 19., 19.]},
                'features': features}
            original = deepcopy(request)
            prepared = self.punch.prepare(request)
            self.assertEqual(request, original)
            yield from prepared['features']

    def test_current_two_left_right_and_three_use_frozen_shallow_edge_sockets(self):
        total, rotations = 0, set()
        for values, _, allocation, plan in self.cases:
            with self.subTest(face=values['faceType'], side=values['sidePosition']):
                features = list(self.prepared_corner_features(allocation, plan))
                self.assertEqual(len(features), 4 if values['faceType'] == 'two' else 8)
                for feature in features:
                    total += 1
                    parameters = feature['frozenTool']['parameters']
                    self.assertTrue(parameters['allowSideOpening'])
                    self.assertFalse(parameters['allowEndOpening'])
                    self.assertNotIn('allowSideOpening', feature['frozenTool']['context']['feature'])
                    graph = {node['key']: node for node in
                             feature['frozenTool']['geometry']['model']['geometry']}
                    slots = [graph['socket-' + str(index) + '-profile'] for index in range(2)]
                    across = 1 if feature['face'] in ('top', 'bottom') else 2
                    axial = parameters['pairRotation'] == 90
                    rotations.add(parameters['pairRotation'])
                    coordinates = [node['arguments']['placement']['origin'][0 if axial else across]
                                   - (feature['station'] if axial else 0.) for node in slots]
                    self.assertAlmostEqual(min(coordinates), -18.4)
                    self.assertAlmostEqual(max(coordinates), 18.4)
                    for index, slot in enumerate(slots):
                        placement = slot['arguments']['placement']
                        self.assertAlmostEqual(placement['origin'][across if axial else 0],
                                               0. if axial else feature['station'])
                        path = slot['arguments']['contours'][0]
                        points = [segment[name] for segment in path['segments']
                                  for name in ('start', 'middle', 'end') if name in segment]
                        self.assertAlmostEqual(max(point[0] for point in points)
                                               - min(point[0] for point in points), 9.6)
                        self.assertAlmostEqual(max(point[1] for point in points)
                                               - min(point[1] for point in points), 1.6)
                        vector = graph['socket-' + str(index)]['arguments']['vector']
                        self.assertAlmostEqual(math.sqrt(sum(value * value for value in vector)),
                                               12. if axial else 12.02)
                        self.assertLess(math.sqrt(sum(value * value for value in vector)), 36.8)
        self.assertEqual(rotations, {0, 90})
        self.assertEqual(total, 16)

    def test_public_input_and_returned_parameters_keep_only_current_product_fields(self):
        contract = load('security_window_product_contract')
        for values, design, allocation, _ in self.cases:
            original = deepcopy(values)
            result = contract._design(values, {'template': self.descriptor,
                'geometryPurpose': 'display'}, self.product)
            self.assertEqual(values, original)
            self.assertEqual(result['parameters'], original)
            self.assertEqual(set(result['parameters']), set(self.defaults))
            self.assertNotIn('allowSideOpening', result['parameters'])
            self.assertEqual(self.product.display(values), design)
            for item in allocation['model']['items']:
                if item['key'].startswith('outer_frame.'):
                    profile = item['properties']['tubeDesigner.profile']
                    self.assertEqual((profile['width'], profile['depth'], profile['wallThickness']),
                                     (38., 38., 1.2))

    def test_internal_socket_policy_is_hidden_and_standalone_default_is_closed(self):
        descriptor = json.loads((ROOT.parent / 'mold/paired-side-slots/tool.json').read_text(encoding='utf-8'))
        policy = next(value for value in descriptor['parameters'] if value['key'] == 'allowSideOpening')
        self.assertIs(policy['defaultValue'], False)
        self.assertIs(policy['presentation']['visible'], False)
        profile = {'contours': self.product._profile(self.defaults, 'frame').contours()}
        request = {'targetSection': profile, 'bounds': {'min': [0., -19., -19.], 'max': [1200., 19., 19.]},
            'features': [{'id': 'closed', 'toolRef': {'id': 'paired-side-slots'}, 'toolTarget': 'part',
                'toolParameters': {'pairCount': 2, 'tabWidth': 8., 'tabLength': 12., 'sideClearance': .2,
                                   'pairRotation': 0., 'allowEndOpening': False},
                'face': 'top', 'reference': 'start', 'station': 19., 'offset': 0., 'rotation': 0.,
                'section': {'profile': profile}}]}
        original = deepcopy(request)
        with self.assertRaisesRegex(ValueError, '平直侧壁'):
            self.punch.prepare(request)
        self.assertEqual(request, original)

    def test_only_continuous_tabs_relax_the_complete_port_check(self):
        call = self.corner()
        self.assertTrue(self.evaluate(call)['applicable'])
        for changes in ({'cJoint': 'insert'},):
            with self.subTest(changes=changes):
                result = self.evaluate(call, **changes)
                self.assertFalse(result['applicable'])
                self.assertIn('C 端口', result['reason'])
        # The product's bottom-wall corner is deliberately outside the other
        # modes' positive-width tool orientation. Use their real supported
        # pose to exercise the retained complete-port guard itself.
        for ab in ('miter', 'wrap'):
            local = turned_input(0)
            local['parts']['memberC']['parameters'].update(width=40, depth=25)
            original = deepcopy(local)
            result = self.runtime.evaluate_process('orthogonal-corner', local,
                {'abJoint': ab, 'cJoint': 'tabs'})
            self.assertFalse(result['applicable'])
            self.assertIn('C 端口', result['reason'])
            self.assertEqual(local, original)
        enlarged = deepcopy(call)
        enlarged['processInput']['parts']['memberC']['section']['profile'] = self.product._profile(
            {**self.defaults, 'frameDepth': 40.}, 'frame').properties()
        result = self.evaluate(enlarged)
        self.assertFalse(result['applicable'])
        self.assertIn('插舌壁面超出', result['reason'])
        for depth, reason in ((1.2, '近侧壁'), (19., '不能越过节点中心')):
            result = self.evaluate(call, tabLength=depth)
            self.assertFalse(result['applicable'])
            self.assertIn(reason, result['reason'])

    def test_bound_quarter_turn_uses_the_physical_manufacturing_z_skin_pair(self):
        actual = participants()
        c = actual[2]
        original_frame = c['section']['sectionFrame']
        c['section'] = section(30., 50., 2., original_frame['xAxisWorld'], original_frame['yAxisWorld'])
        values = {'abJoint': 'continuous', 'cJoint': 'tabs', 'tabWidth': 8., 'tabLength': 12.}
        with self.assertRaisesRegex(ValueError, '插舌壁面超出'):
            self.runtime.resolve_bound_plan('orthogonal-corner', values, participants=actual,
                                            product_topology='orthogonal-corner')
        frame = c['nodeFrame']
        matrix, inverse = frame['sourceToPart'], frame['partToSource']
        matrix[4:8], matrix[8:12] = matrix[8:12], [-value for value in matrix[4:8]]
        for row in range(4):
            inverse[4 * row + 1], inverse[4 * row + 2] = inverse[4 * row + 2], -inverse[4 * row + 1]
        for key in ('selfAwayPart', 'selfNodePart'):
            x, y, z = frame[key]
            frame[key] = [x, z, -y]
        frame['awayPartsByRole'] = {key: [value[0], value[2], -value[1]]
                                   for key, value in frame['awayPartsByRole'].items()}
        saved = deepcopy(actual)
        result = self.runtime.resolve_bound_plan('orthogonal-corner', values, participants=actual,
                                                product_topology='orthogonal-corner')
        self.assertTrue(result['supported'])
        self.assertEqual(actual, saved)
        c_part = next(value for value in result['parts'] if value['role'] == 'memberC')
        self.assertAlmostEqual(c_part['ends']['start']['trim'], 28.)
        for part in result['parts']:
            for feature in part['features']:
                if feature['toolRef']['id'] == 'paired-side-slots':
                    self.assertTrue(feature['toolParameters']['allowSideOpening'])
                    self.assertFalse(feature['toolParameters']['allowEndOpening'])

    def test_direct_rectangular_post_apertures_use_the_same_parameter_entry(self):
        allocation = grid_window('tabs', 'tabs', foldedPostJoint='tabs')[-1]
        post = load('assembly_post_machining')
        tool = post._tool('paired-side-slots')
        calls = [call for call in allocation['assemblySource']['instances']
                 if call['templateId'] == 'tube-post-aperture'
                 and not call['processInput']['parts']['branch']['id'].startswith('main_grid.vertical.')]
        self.assertTrue(calls)
        with patch.object(tool, 'generate', wraps=tool.generate) as generated:
            for call in calls:
                saved = deepcopy(call)
                result = post.tube_post_aperture(call['processInput'], call['parameters'])
                self.assertTrue(result['applicable'], result['reason'])
                self.assertEqual(call, saved)
                self.assertEqual(result['parameters'], call['parameters'])
            self.assertEqual(generated.call_count, len(calls))
            for parameters, context in (call.args for call in generated.call_args_list):
                self.assertIs(parameters['allowSideOpening'], True)
                self.assertNotIn('allowSideOpening', context['feature'])


if __name__ == '__main__':
    unittest.main()
