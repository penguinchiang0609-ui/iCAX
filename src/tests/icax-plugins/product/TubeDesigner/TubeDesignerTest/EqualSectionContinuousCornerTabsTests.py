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

    def test_current_two_left_right_and_three_use_frozen_near_half_shell_sockets(self):
        total,rotations=0,set()
        for values,_,allocation,plan in self.cases:
            features=list(self.prepared_corner_features(allocation,plan))
            self.assertEqual(len(features),4 if values['faceType']=='two' else 8)
            for feature in features:
                total+=1;parameters=feature['frozenTool']['parameters']
                self.assertNotIn('allowSideOpening',parameters)
                self.assertNotIn('allowEndOpening',parameters)
                graph={n['key']:n for n in feature['frozenTool']['geometry']['model']['geometry']}
                self.assertEqual(len(graph['mating-ring.profile']['arguments']['contours']),2)
                vector=graph['mating-ring']['arguments']['vector']
                self.assertAlmostEqual(math.sqrt(sum(v*v for v in vector)),19.02)
                self.assertEqual(graph['tool']['arguments']['operation'],'intersect')
                rotations.add(parameters['pairRotation'])
        self.assertEqual(rotations,{0,90});self.assertEqual(total,16)

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

    def test_mother_survival_has_no_hidden_flat_or_edge_permission(self):
        descriptor=json.loads((ROOT.parent/'mold/paired-side-slots/tool.json').read_text(encoding='utf-8'))
        keys={v['key'] for v in descriptor['parameters']}
        self.assertNotIn('allowSideOpening',keys)
        self.assertNotIn('allowEndOpening',keys)
        self.assertIn('tabAngle',keys)

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
                    self.assertNotIn('allowSideOpening',feature['toolParameters'])
                    self.assertNotIn('allowEndOpening',feature['toolParameters'])

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
                self.assertNotIn('allowSideOpening',parameters)
                self.assertNotIn('allowSideOpening', context['feature'])


if __name__ == '__main__':
    unittest.main()
