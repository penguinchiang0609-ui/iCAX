from copy import deepcopy
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[5]
RUNTIME = ROOT / 'iCAX-Engine/framework/TemplateRuntime/python'
sys.path.insert(0, str(RUNTIME))
from icax_template_sdk.profile_constraints import (
    _profile_validation_scope, validate_product_profile_parameters,
    validate_selected_product_profile)
import icax_template_worker as worker


class ProductProfileConstraintsTests(unittest.TestCase):
    def setUp(self):
        self.template = {
            'parameters': [{'key': 'frameProfileType', 'valueType': 'enum', 'choices': [{'value': 'rect'}],
                'presentation': {'profileConstraints': {'sectionKinds': ['rect'],
                    'hollow': True, 'minimumContourCount': 2, 'maximumContourCount': 2}}}],
            'extensions': {'resourceRoles': {'profiles': {'frame': {'parameter': 'frameProfileType'}}}},
        }
        self.snapshot = {'schema': 'icax.imported-tube-profile', 'schemaVersion': 1,
            'kind': 'fixed-section', 'sectionKind': 'rect', 'contours': [{}, {}], 'hollow': True}
        self.values = {'frameProfileType': 'rect', 'tubeDesignerProfileOverrides': {'frame': self.snapshot}}

    def validate(self):
        validate_product_profile_parameters(self.template, self.values)
        with _profile_validation_scope(self.template):
            for role, snapshot in self.values['tubeDesignerProfileOverrides'].items():
                validate_selected_product_profile(role, snapshot)

    def test_valid_snapshot_does_not_mutate_inputs(self):
        before = deepcopy((self.template, self.values))
        self.validate()
        self.assertEqual((self.template, self.values), before)

    def test_kind_is_section_kind_and_resource_kind_does_not_grant_acceptance(self):
        for value in ('round', 'arbitrary', '', None):
            with self.subTest(value=value):
                self.snapshot['sectionKind'] = value
                with self.assertRaises(ValueError): self.validate()
        self.snapshot.pop('sectionKind')
        self.snapshot['kind'] = 'rect'
        with self.assertRaises(ValueError): self.validate()

    def test_override_requires_whitelist_and_declared_role(self):
        self.template['parameters'][0]['presentation']['profileConstraints'].pop('sectionKinds')
        with self.assertRaises(ValueError): self.validate()
        self.setUp()
        self.values['tubeDesignerProfileOverrides'] = {'other': self.snapshot}
        with self.assertRaises(ValueError): self.validate()

    def test_hollow_and_count_use_actual_contours(self):
        self.snapshot['contours'] = [{}]
        self.snapshot['hollow'] = False
        with self.assertRaises(ValueError): self.validate()
        self.setUp()
        self.snapshot['contourCount'] = 1
        with self.assertRaises(ValueError): self.validate()
        self.snapshot['contourCount'] = 2
        self.snapshot['hollow'] = False
        with self.assertRaises(ValueError): self.validate()
        self.snapshot['hollow'] = True
        self.snapshot['contours'].append({})
        with self.assertRaises(ValueError): self.validate()

    def test_bad_declarations_and_choices_are_rejected(self):
        for change in ({'sectionKinds': []}, {'sectionKinds': None}, {'sectionKinds': ['rect', 'rect']},
                {'sectionKinds': ['round']}, {'hollow': 1}, {'minimumContourCount': True},
                {'minimumContourCount': 2.0}, {'minimumContourCount': None},
                {'maximumContourCount': None}, {'minimumContourCount': 1001},
                {'minimumContourCount': 3, 'maximumContourCount': 2}):
            with self.subTest(change=change):
                self.setUp()
                self.template['parameters'][0]['presentation']['profileConstraints'].update(change)
                with self.assertRaises(ValueError): self.validate()

    def test_worker_rejects_before_loading_or_calling_template(self):
        self.values['tubeDesignerProfileOverrides'] = {'unknown': self.snapshot}
        request = {'template': self.template, 'parameters': self.values, 'context': {'geometryPurpose': 'display'}}
        with patch.object(worker, '_load_template') as loader:
            with self.assertRaises(ValueError): worker._evaluate(request)
            loader.assert_not_called()

    def test_closed_parent_keeps_inapplicable_profile_draft_and_active_lookup_rejects(self):
        def display(parameters):
            if parameters['hasDoor']:
                validate_selected_product_profile('frame', parameters['tubeDesignerProfileOverrides']['frame'])
            return {'parameters': deepcopy(parameters)}
        module = SimpleNamespace(display=display, manufacturing=lambda parameters: {})
        for section_kind in ('round', 'arbitrary'):
            self.snapshot['sectionKind'] = section_kind
            self.values['hasDoor'] = False
            before = deepcopy(self.values)
            request = {'template': self.template, 'parameters': self.values,
                'context': {'geometryPurpose': 'display'}}
            with patch.object(worker, '_load_template', return_value=module):
                self.assertEqual(worker._evaluate(request)['parameters'], before)
                self.assertEqual(self.values, before)
                self.values['hasDoor'] = True
                with self.assertRaisesRegex(ValueError, 'not applicable'):
                    worker._evaluate(request)

    def test_profile_scope_is_required_and_does_not_leak(self):
        with self.assertRaisesRegex(ValueError, 'host template descriptor'):
            validate_selected_product_profile('frame', self.snapshot)
        self.validate()
        with self.assertRaisesRegex(ValueError, 'host template descriptor'):
            validate_selected_product_profile('frame', self.snapshot)

    def test_protocol_consumption_metadata_is_outside_public_result(self):
        def display(parameters):
            validate_selected_product_profile('frame', parameters['tubeDesignerProfileOverrides']['frame'])
            validate_selected_product_profile('frame', parameters['tubeDesignerProfileOverrides']['frame'])
            return {'parameters': deepcopy(parameters)}
        request = {'protocol': worker.PROTOCOL, 'protocolVersion': worker.PROTOCOL_VERSION,
            'operation': 'evaluate', 'requestId': 7, 'template': self.template, 'parameters': self.values,
            'context': {'geometryPurpose': 'display'}}
        with patch.object(worker, '_load_template', return_value=SimpleNamespace(display=display)):
            envelope, shutdown = worker._handle(request)
        self.assertFalse(shutdown)
        self.assertEqual(envelope['profileRolesConsumed'], ['frame'])
        self.assertNotIn('profileRolesConsumed', envelope['result'])
        self.assertEqual(envelope['result']['parameters'], self.values)

    def test_product_requires_purpose_and_generic_profile_entry_remains(self):
        module = SimpleNamespace(display=lambda parameters: {}, manufacturing=lambda parameters: {},
            generate=lambda parameters, context: {'parameters': parameters})
        request = {'template': {}, 'parameters': {}, 'context': {}}
        with patch.object(worker, '_load_template', return_value=module):
            with self.assertRaisesRegex(ValueError, 'geometryPurpose'):
                worker._evaluate(request)
        module = SimpleNamespace(generate=lambda parameters, context: {'parameters': parameters})
        with patch.object(worker, '_load_template', return_value=module):
            self.assertEqual(worker._evaluate(request), {'parameters': {}})

    def test_display_and_manufacturing_v1_are_rejected(self):
        from icax_template_sdk.display import expand_display_model, to_display_model
        from icax_template_sdk.manufacturing import expand_manufacturing_model
        for function in (expand_display_model, to_display_model):
            with self.assertRaises(ValueError):
                function({'schema': 'icax.display-model', 'schemaVersion': 1})
        with self.assertRaises(ValueError):
            expand_manufacturing_model({'schema': 'icax.manufacturing-model', 'schemaVersion': 1})

    def test_current_compose_declares_fixed_geometry_sources_without_mutating_design(self):
        from icax_template_sdk.manufacturing import compose_manufacturing_model
        design = {'schema': 'icax.display-model', 'schemaVersion': 2,
            'coordinateSystem': 'right-handed-x-width-y-depth-z-height', 'lengthUnit': 'mm',
            'resources': [{'key': 'section', 'operator': 'profile2d', 'inputs': [],
                'arguments': {'placement': {'origin': [0, 0, 0], 'xAxis': [1, 0, 0], 'yAxis': [0, 1, 0]},
                    'contours': [{'kind': 'path', 'closed': True, 'segments': [
                        {'kind': 'line', 'start': [0, 0], 'end': [10, 0]},
                        {'kind': 'line', 'start': [10, 0], 'end': [10, 6]},
                        {'kind': 'line', 'start': [10, 6], 'end': [0, 6]},
                        {'kind': 'line', 'start': [0, 6], 'end': [0, 0]}]}]}},
                {'key': 'solid', 'operator': 'extrude', 'inputs': ['section'],
                 'arguments': {'vector': [0, 0, 40]}}],
            'items': [{'key': 'group', 'displayName': 'Group', 'children': ['tube', 'glass', 'accessory'],
                       'properties': {}},
                      *[{'key': kind, 'displayName': kind, 'children': [],
                         'geometry': {'resource': 'solid'},
                         'properties': {'partKind': kind}}
                        for kind in ('tube', 'glass', 'accessory')]],
            'roots': ['group'], 'annotations': []}
        declaration = {'schema': 'icax.manufacturing-model', 'schemaVersion': 4,
            'connections': [], 'processes': []}
        before, declaration_before = deepcopy(design), deepcopy(declaration)
        result = compose_manufacturing_model(declaration, design)
        self.assertEqual(result['sourceMappings'], [
            {'itemKey': kind, 'sources': [{'itemKey': kind}]}
            for kind in ('tube', 'glass', 'accessory')])
        private = {item['key']: item for item in result['items']}
        for kind in ('tube', 'glass', 'accessory'):
            self.assertEqual(private[kind]['properties']['manufacturing.partKind'], kind)
        self.assertEqual(design, before)
        self.assertEqual(declaration, declaration_before)
        self.assertNotIn('sourceMappings', design)


if __name__ == '__main__':
    unittest.main()
