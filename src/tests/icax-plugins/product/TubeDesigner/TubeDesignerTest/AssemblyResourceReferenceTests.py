"""Current scoped and pinned assembly resources; no CAD or deployment work."""
from copy import deepcopy
import json
import unittest

from WindowCatalogueTests import package, SRC
from WindowManufacturingInputTests import load
import icax_template_worker as worker
from icax_template_sdk.manufacturing import (
    compose_manufacturing_model, validate_assembly_resource_ref, validate_manufacturing_model,
)


class AssemblyResourceReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, defaults, unused = package('single_face_security_window')
        cls.punch = load('punch_tool_runtime')
        cls.runtime = load('assembly_template_runtime')
        cls.window = load('assembly_window_process')
        cls.tool, unused, unused, unused, digest = cls.punch._package(cls.punch.ROOT / 'v-notch-sharp')
        cls.reference = {'scope': 'system', 'id': cls.tool['id'],
                         'version': cls.tool['version'], 'digest': digest}
        cls.parameters = {**defaults, 'faceType': 'single', 'width': 1200., 'height': 1000.,
                          'accessDoorEnabled': False, 'frameManufacturingMode': 'plane_v_notch',
                          'outerFrameGrooveTool': 'system:v-notch-sharp', 'spacing': 500.,
                          'horizontalBranchReserve': 5., 'verticalBranchReserve': 10.,
                          'tubeDesignerToolBindings': {'outerFrameGroove': {
                              'schema': 'icax.product-resource-binding', 'schemaVersion': 1,
                              'resourceKind': 'punch-tool', 'role': 'outerFrameGroove',
                              'selectionKey': 'system:v-notch-sharp', 'ref': deepcopy(cls.reference),
                              'parameters': {}, 'snapshot': {
                                  'displayName': cls.tool['displayName'], 'kind': cls.tool['kind'],
                                  'target': cls.tool['target'], 'category': cls.tool.get('category', ''),
                                  'targetProfileRole': 'frame'}}}}
        saved = deepcopy(cls.parameters)
        template_path = SRC / 'apps/tube-designer/templates/product/single_face_security_window/template.py'
        cls.request = {'templatePath': str(template_path), 'template': cls.descriptor,
                       'parameters': cls.parameters, 'context': {'geometryPurpose': 'manufacturing'}}
        cls.declaration = worker._evaluate(deepcopy(cls.request))
        display_request = deepcopy(cls.request)
        display_request['context']['geometryPurpose'] = 'display'
        cls.design = worker._evaluate(display_request)
        cls.composed = compose_manufacturing_model(cls.declaration, cls.design)
        if json.dumps(cls.parameters, sort_keys=True) != json.dumps(saved, sort_keys=True):
            raise AssertionError('worker preparation changed the public input parameters')

    def test_scopes_and_optional_pins_are_validated_without_mutation(self):
        for scope in ('system', 'user', 'template'):
            base = {'scope': scope, 'id': 'tool'}
            if scope == 'template':
                base['templateId'] = 'product'
            for pins in ({}, {'version': '1'}, {'digest': 'package-hash'},
                         {'version': '1', 'digest': 'package-hash'}):
                with self.subTest(scope=scope, pins=pins):
                    reference = {**base, **pins}
                    saved = deepcopy(reference)
                    self.assertIs(validate_assembly_resource_ref(reference), reference)
                    self.assertEqual(reference, saved)

    def test_unknown_missing_and_malformed_fields_are_rejected(self):
        invalid = [None, [], {'scope': 'unknown', 'id': 'tool'}, {'scope': 'system'},
                   {'scope': 'system', 'id': ''}, {'scope': 'system', 'id': 3},
                   {'scope': 'template', 'id': 'tool'},
                   {'scope': 'template', 'id': 'tool', 'templateId': ''},
                   {'scope': 'template', 'id': 'tool', 'templateId': 3},
                   {'scope': 'system', 'id': 'tool', 'templateId': 'product'},
                   {**self.reference, 'libraryScope': 'system'},
                   {**self.reference, 'unexpected': True}]
        invalid += [{**self.reference, key: value}
                    for key in ('version', 'digest') for value in ('', None, 1, False, [], {})]
        for reference in invalid:
            with self.subTest(reference=reference):
                saved = deepcopy(reference)
                with self.assertRaisesRegex(ValueError, 'complete scoped reference'):
                    validate_assembly_resource_ref(reference)
                self.assertEqual(reference, saved)

    def test_both_manufacturing_validators_accept_the_real_ui_pins(self):
        for original in (self.declaration, self.composed):
            with self.subTest(schemaVersion=original['schemaVersion']):
                document = deepcopy(original)
                saved = deepcopy(document)
                self.assertEqual(document['processes'][0]['definition']['processInput']
                                 ['resources']['outerFrameGroove']['ref'], self.reference)
                self.assertIs(validate_manufacturing_model(document), document)
                self.assertEqual(document, saved)

    def test_both_manufacturing_validators_reject_illegal_refs_and_slots(self):
        for original in (self.declaration, self.composed):
            for value in ({**self.reference, 'unexpected': True},
                          {**self.reference, 'digest': ''}, {**self.reference, 'version': 1},
                          {'scope': 'template', 'id': 'tool'}):
                with self.subTest(schemaVersion=original['schemaVersion'], reference=value):
                    document = deepcopy(original)
                    document['processes'][0]['definition']['processInput']['resources'] = {
                        'outerFrameGroove': {'ref': value}}
                    saved = deepcopy(document)
                    with self.assertRaisesRegex(ValueError, 'complete scoped reference'):
                        validate_manufacturing_model(document)
                    self.assertEqual(document, saved)
            for slot in ({}, {'ref': deepcopy(self.reference), 'parameters': {}}, []):
                with self.subTest(schemaVersion=original['schemaVersion'], slot=slot):
                    document = deepcopy(original)
                    document['processes'][0]['definition']['processInput']['resources'] = {'outerFrameGroove': slot}
                    with self.assertRaises(ValueError):
                        validate_manufacturing_model(document)

    def test_window_planner_preserves_the_selected_pinned_reference(self):
        document = deepcopy(self.composed)
        saved = deepcopy(document)
        compiled = self.window.compile_window_assembly(document)
        refs = [call['definition']['processInput'].get('resources', {}).get('node-slot', {}).get('ref')
                for call in compiled['processes']]
        selected = [reference for reference in refs if reference is not None]
        self.assertGreater(len(selected), 0)
        self.assertTrue(all(reference == self.reference for reference in selected))
        self.assertEqual(document, saved)

    def test_window_keeps_its_current_system_and_user_scope_limit(self):
        document = deepcopy(self.composed)
        document['processes'][0]['definition']['processInput']['resources']['outerFrameGroove']['ref'] = {
            'scope': 'template', 'id': 'v-notch-sharp', 'templateId': self.descriptor['id']}
        with self.assertRaisesRegex(ValueError, 'selected window assembly tool reference'):
            self.window.compile_window_assembly(document)

    def test_current_resource_resolution_checks_both_pins(self):
        process = {'id': 'node-slot', 'resource': {'descriptor': self.tool}}
        resources = {'node-slot': {'ref': deepcopy(self.reference)}}
        saved = deepcopy(resources)
        descriptor, reference, unused = self.runtime._function_process_resource(process, {}, resources, {})
        self.assertEqual(descriptor['id'], self.reference['id'])
        self.assertEqual(reference, self.reference)
        self.assertEqual(resources, saved)
        for pin, invalid, message in (('version', 'obsolete-version', '版本已改变'),
                                      ('digest', '0' * 64, '内容已改变')):
            with self.subTest(pin=pin):
                resources = {'node-slot': {'ref': {**self.reference, pin: invalid}}}
                saved = deepcopy(resources)
                validate_assembly_resource_ref(resources['node-slot']['ref'])
                with self.assertRaisesRegex(ValueError, message):
                    self.runtime._function_process_resource(process, {}, resources, {})
                self.assertEqual(resources, saved)

    def test_worker_executes_the_real_ui_pinned_window_declaration(self):
        request = {'manufacturingDefinition': deepcopy(self.declaration), 'designModel': deepcopy(self.design),
                   'context': {'sharedRoot': str(SRC / 'apps/tube-designer/templates/_shared')}}
        saved = json.dumps(request, sort_keys=True)
        result = worker._execute_manufacturing(request)
        source = result['extensions']['tubeDesigner.assemblyProcessSource']
        references = [slot['ref'] for instance in source['instances']
                      for slot in instance['processInput'].get('resources', {}).values()
                      if slot['ref']['id'] == self.reference['id']]
        self.assertGreater(len(references), 0)
        self.assertTrue(all(reference == self.reference for reference in references))
        self.assertEqual(json.dumps(request, sort_keys=True), saved)

    def test_worker_inputs_and_public_documents_keep_current_parameter_contract(self):
        request = deepcopy(self.request)
        serialized_before = json.dumps(request['parameters'], sort_keys=True)
        result = worker._evaluate(request)
        self.assertEqual(json.dumps(request['parameters'], sort_keys=True), serialized_before)
        self.assertEqual(result, self.declaration)
        # The host owns the public parameters envelope; pure v2/v4 documents do
        # not return or rename it. Native echo needs separate host interface tests.
        self.assertNotIn('parameters', result)
        self.assertNotIn('parameters', self.design)
        self.assertEqual(result['processes'][0]['definition']['processInput']
                         ['resources']['outerFrameGroove']['ref'], self.reference)


if __name__ == '__main__':
    unittest.main()
