"""One frame material exposes weld or retained tabs, never full-tube insertion."""
from copy import deepcopy
import unittest

from WindowManufacturingInputTests import (
    package, load, forbid_product_processing, forbid_product_callbacks,
)
from FoldedPostConnectionTests import allocate, full_plan
from AssemblyContinuousCornerInsertionTests import turned_input
from icax_template_sdk.manufacturing import compose_manufacturing_model


class EqualSectionProductPostChoicesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.product = package('single_face_security_window')
        cls.contract = load('security_window_product_contract')
        cls.runtime = load('assembly_template_runtime')

    def values(self, joint='weld', **changes):
        return {**deepcopy(self.defaults), 'faceType': 'three', 'width': 1200.,
                'sideWidth': 600., 'height': 1800., 'frameManufacturingMode': 'plane_v_notch',
                'outerFrameGrooveTool': 'system:v-notch-sharp', 'accessDoorEnabled': False,
                'foldedPostJoint': joint, **changes}

    def test_current_descriptor_has_only_equal_section_choices_and_no_direct_parameters(self):
        fields = {field['key']: field for field in self.descriptor['parameters']}
        self.assertEqual([choice['value'] for choice in fields['foldedPostJoint']['choices']],
                         ['weld', 'tabs'])
        self.assertEqual(fields['foldedPostJoint']['defaultValue'], 'weld')
        self.assertNotIn('foldedPostInsertDepth', fields)
        self.assertNotIn('foldedPostFitGap', fields)
        self.assertEqual(self.product.TEMPLATE_VERSION, self.descriptor['version'])
        for key in ('horizontalEndConnection', 'verticalEndConnection'):
            self.assertIn('insert', [choice['value'] for choice in fields[key]['choices']])

    def test_invalid_product_insertion_is_rejected_without_mapping_or_input_mutation(self):
        values = self.values('insert')
        original = deepcopy(values)
        with self.assertRaisesRegex(ValueError, '横竖统一管材'):
            self.product.manufacturing(values)
        self.assertEqual(values, original)
        self.assertEqual(values['foldedPostJoint'], 'insert')

    def test_weld_ignores_inactive_tab_depth_and_returns_exact_host_parameters(self):
        # This legal host draft exceeds the selected tabs' physical depth.
        # An inactive tabs choice must not block the weld declaration.
        values = self.values('weld', foldedPostTabLength=90.)
        original = deepcopy(values)
        with forbid_product_processing():
            document = self.product.manufacturing(values)
            display = self.contract._design(values, {'template': self.descriptor,
                'geometryPurpose': 'display'}, self.product)
        self.assertEqual(values, original)
        self.assertEqual(display['parameters'], original)
        self.assertEqual(set(display['parameters']), set(self.defaults))
        choices = document['processes'][0]['definition']['parameters']
        self.assertEqual(choices['foldedPostJoint'], 'weld')
        self.assertEqual(choices['foldedPostTabLength'], 0.)
        for key in ('foldedPostInsertDepth', 'foldedPostFitGap'):
            self.assertNotIn(key, display['parameters'])
            self.assertEqual(choices[key], 0.)
        tabs = {**deepcopy(values), 'foldedPostJoint': 'tabs', 'foldedPostTabLength': 12.}
        with forbid_product_processing():
            self.assertEqual(self.product.display(values), self.product.display(tabs))

    def test_current_three_face_v_fold_both_choices_use_real_equal_materials(self):
        for joint in ('weld', 'tabs'):
            with self.subTest(joint=joint):
                values = self.values(joint)
                original = deepcopy(values)
                design, declaration = self.product.display(values), self.product.manufacturing(values)
                with forbid_product_callbacks():
                    compiled = load('assembly_window_process').compile_window_assembly(
                        compose_manufacturing_model(declaration, design))
                    before = deepcopy(compiled)
                    allocation = allocate(compiled)
                    allocated = deepcopy(allocation)
                    plan = full_plan(allocation)
                self.assertEqual(values, original)
                self.assertEqual(compiled, before)
                self.assertEqual(allocation, allocated)
                calls = [call for call in allocation['assemblySource']['instances']
                         if call['templateId'] == 'orthogonal-corner']
                self.assertEqual(len(calls), 4)
                for call in calls:
                    self.assertEqual(call['parameters']['abJoint'], 'continuous')
                    self.assertEqual(call['parameters']['cJoint'], joint)
                    for part in call['processInput']['parts'].values():
                        profile = part['section']['profile']
                        self.assertEqual((profile['width'], profile['depth'], profile['wallThickness']),
                                         (38., 38., 1.2))
                self.assertTrue(plan['operations'])
                for item in allocation['model']['items']:
                    if item['key'].startswith('outer_frame.'):
                        profile = item['properties']['tubeDesigner.profile']
                        self.assertEqual((profile['width'], profile['depth'], profile['wallThickness']),
                                         (38., 38., 1.2))

    def test_generic_different_section_insertion_still_uses_strict_geometry_check(self):
        helper = load('security_window_post_connections')
        choices = helper.process_choices({'faceType': 'three', 'frameManufacturingMode': 'plane_v_notch',
                                         'foldedPostJoint': 'insert', 'foldedPostInsertDepth': 8.,
                                         'foldedPostFitGap': .2})
        parameters = helper.node_parameters(choices)
        local = turned_input(1)
        original = deepcopy(local)
        result = self.runtime.evaluate_process('orthogonal-corner', local, parameters)
        self.assertTrue(result['applicable'], result['reason'])
        self.assertEqual(local, original)
        self.assertEqual(parameters['cJoint'], 'insert')
        invalid = deepcopy(local)
        invalid['parts']['memberC']['parameters'].update(width=40., depth=25.)
        result = self.runtime.evaluate_process('orthogonal-corner', invalid, parameters)
        self.assertFalse(result['applicable'])
        self.assertIn('C 端口', result['reason'])


if __name__ == '__main__':
    unittest.main()
