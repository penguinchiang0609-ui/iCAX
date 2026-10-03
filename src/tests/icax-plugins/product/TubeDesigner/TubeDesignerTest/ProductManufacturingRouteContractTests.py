"""Route planners consume stable design and publish explicit generated sets."""
from copy import deepcopy
import unittest

from icax_template_sdk.manufacturing import (validate_manufacturing_model, initial_manufacturing_model,
                                           compose_manufacturing_model)
from icax_template_sdk.display import expand_display_model
from ProductManufacturingDeclarationTests import execute_definition


def design_document():
    return {
        'schema': 'icax.manufacturing-model', 'schemaVersion': 3,
        'coordinateSystem': 'right-handed-x-width-y-depth-z-height', 'lengthUnit': 'mm',
        'resources': [{'key': 'section', 'operator': 'profile2d', 'inputs': [],
                       'arguments': {'contours': [{'kind': 'polygon', 'points': [[-5,-5],[5,-5],[5,5],[-5,5]]}]}}],
        'items': [{'key': 'design.rail', 'displayName': 'Design rail', 'children': [],
                   'properties': {'tubeDesigner.profile': {'sectionResource': 'section'}},
                   'designInput': {'kind': 'tube-member', 'profileResource': 'section',
                                   'start': [0,0,0], 'end': [0,0,100],
                                   'sectionFrame': {'origin': [0,0,0], 'xAxis': [1,0,0],
                                                    'yAxis': [0,1,0], 'zAxis': [0,0,1]}}}],
        'roots': ['design.rail'],
        'sourceMappings': [{'itemKey': 'design.rail', 'sources': [{'itemKey': 'display.rail'}]}],
        'processes': [{'key': 'route', 'kind': 'assembly-process', 'definition': {
            'templateId': 'route-provider',
            'processInput': {'schema': 'icax.assembly-process-input', 'schemaVersion': 3,
                             'parts': {'members': {'scope': 'design', 'itemKeys': ['design.rail']}},
                             'geometry': {}},
            'parameters': {}, 'processDrafts': {},
            'outputs': {'manufacturing': {'kind': 'manufacturing-set', 'key': 'route.parts'}},
            'dependencies': []}}]
    }


def shared_design():
    """A real display resource tree is the only product geometry input."""
    legacy = design_document()
    section = deepcopy(legacy['resources'][0])
    solid = {'key':'solid','operator':'extrude','inputs':['section'],'arguments':{'vector':[0,0,100]}}
    items = []
    for key, origin in (('design.rail',[0,0,0]),('design.mate',[20,0,0])):
        end = [origin[0],origin[1],100]
        frame = {'originAtStart':origin,'xAxis':[1,0,0],'yAxis':[0,1,0],'zAxis':[0,0,1],
                 'centerlineUV':[0,0]}
        items.append({'key':key,'displayName':key,'children':[],
                      'geometry':{'resource':'solid','placement':{'origin':origin,'xAxis':[1,0,0],
                                                                 'yAxis':[0,1,0],'zAxis':[0,0,1]}},
                      'properties':{'tubeDesigner.profile':{'sectionResource':'section',
                                                            'contours':deepcopy(section['arguments']['contours'])},
                                    'assemblyFrame.member':{'start':origin,'end':end,'axisLength':100,
                                                            'sectionFrame':frame}}})
    return {'schema':'icax.display-model','schemaVersion':2,
            'coordinateSystem':legacy['coordinateSystem'],'lengthUnit':'mm',
            'resources':[section,solid],'items':items,'roots':[item['key'] for item in items],'annotations':[]}


def shared_manufacturing():
    process = deepcopy(design_document()['processes'][0])
    process['definition']['processInput']['parts']['members']['itemKeys'] = ['design.rail','design.mate']
    return {'schema':'icax.manufacturing-model','schemaVersion':4,
            'connections':[{'key':'join','kind':'assembly','items':['design.rail','design.mate'],'properties':{}}],
            'processes':[process]}


class ProductManufacturingRouteContractTests(unittest.TestCase):
    def test_shared_display_is_the_only_geometry_and_connection_definition_is_owned_by_manufacturing(self):
        design, raw = shared_design(), shared_manufacturing()
        before_design, before_raw = deepcopy(design), deepcopy(raw)
        expand_display_model(design)
        validate_manufacturing_model(raw)
        combined = compose_manufacturing_model(raw,design)
        self.assertEqual(set(raw),{'schema','schemaVersion','connections','processes'})
        self.assertNotIn('connections',design)
        self.assertEqual(combined['schemaVersion'],3)
        self.assertEqual(combined['resources'],design['resources'])
        self.assertEqual(combined['roots'],design['roots'])
        self.assertEqual([item['key'] for item in combined['items']],design['roots'])
        self.assertEqual(design,before_design); self.assertEqual(raw,before_raw)

    def test_public_manufacturing_requires_host_design_and_rejects_duplicate_geometry(self):
        raw = shared_manufacturing()
        with self.assertRaisesRegex(ValueError,'requires host display designModel'):
            execute_definition(raw)
        for extra in ('items','resources','roots','parameters','template','sourceMappings'):
            with self.subTest(extra=extra):
                invalid = deepcopy(raw); invalid[extra] = []
                with self.assertRaises(ValueError): validate_manufacturing_model(invalid)

    def test_shared_item_and_connection_references_must_resolve_without_mutating_host_design(self):
        design, raw = shared_design(), shared_manufacturing()
        original = deepcopy(design)
        for failure in ('unknown-member','unknown-connection-item','repeated-connection-key',
                        'same-connection-participant','namespace-occupies-item'):
            with self.subTest(failure=failure):
                invalid = deepcopy(raw)
                if failure == 'unknown-member':
                    invalid['processes'][0]['definition']['processInput']['parts']['members']['itemKeys'] = ['absent']
                elif failure == 'unknown-connection-item':
                    invalid['connections'][0]['items'][1] = 'absent'
                elif failure == 'repeated-connection-key':
                    invalid['connections'].append(deepcopy(invalid['connections'][0]))
                elif failure == 'same-connection-participant':
                    invalid['connections'][0]['items'][1] = invalid['connections'][0]['items'][0]
                else:
                    invalid['processes'][0]['definition']['outputs']['manufacturing']['key'] = 'design.rail'
                with self.assertRaises(ValueError): compose_manufacturing_model(invalid,design)
                self.assertEqual(design,original)

    def test_stable_design_has_no_resolved_part_count_or_manufacturing_paths(self):
        document = design_document()
        original = deepcopy(document)
        self.assertIs(validate_manufacturing_model(document), document)
        self.assertEqual(document, original)
        with self.assertRaisesRegex(ValueError, 'assembly execution'):
            initial_manufacturing_model(document)

    def test_generated_set_can_be_an_explicit_dependent_input(self):
        document = design_document()
        downstream = deepcopy(document['processes'][0])
        downstream['key'] = 'dependent'
        call = downstream['definition']
        call['processInput']['parts'] = {'parts': {'scope': 'process-output',
                                                  'processKey': 'route', 'outputKey': 'manufacturing'}}
        call['outputs']['manufacturing']['key'] = 'dependent.parts'
        call['dependencies'] = ['route']
        document['processes'].append(downstream)
        validate_manufacturing_model(document)
        for failure in ('missing-dependency', 'missing-output', 'namespace-instead-of-role', 'cycle'):
            with self.subTest(failure=failure):
                invalid = deepcopy(document)
                definition = invalid['processes'][1]['definition']
                if failure == 'missing-dependency':
                    definition['dependencies'] = []
                elif failure == 'missing-output':
                    definition['processInput']['parts']['parts']['outputKey'] = 'absent'
                elif failure == 'namespace-instead-of-role':
                    definition['processInput']['parts']['parts']['outputKey'] = 'route.parts'
                else:
                    invalid['processes'][0]['definition']['dependencies'] = ['dependent']
                with self.assertRaises(ValueError):
                    validate_manufacturing_model(invalid)

    def test_design_rejects_stock_dimensions_route_paths_and_invalid_frames(self):
        for failure in ('length', 'stockLength', 'stockStart', 'formingOrder', 'zero-span',
                        'wrong-origin', 'wrong-axis', 'wrong-section', 'unbound-design', 'unknown-source-segment'):
            with self.subTest(failure=failure):
                invalid = design_document()
                member = invalid['items'][0]
                data = member['designInput']
                if failure in ('length', 'stockLength', 'stockStart', 'formingOrder'):
                    member['properties'][failure] = 100
                elif failure == 'zero-span':
                    data['end'] = data['start'][:]
                elif failure == 'wrong-origin':
                    data['sectionFrame']['origin'] = [0,0,1]
                elif failure == 'wrong-axis':
                    data['sectionFrame']['xAxis'] = [0,1,0]
                    data['sectionFrame']['yAxis'] = [0,0,1]
                    data['sectionFrame']['zAxis'] = [1,0,0]
                elif failure == 'wrong-section':
                    data['profileResource'] = 'absent'
                elif failure == 'unbound-design':
                    invalid['processes'] = []
                else:
                    invalid['sourceMappings'][0]['sources'][0]['segments'] = [
                        {'segmentKey': 'manufacturing.segment', 'sourceRange': [0,1]}]
                with self.assertRaises(ValueError):
                    validate_manufacturing_model(invalid)

    def test_generated_outputs_require_unique_namespaces_and_exclusive_output_contract(self):
        for failure in ('empty', 'duplicate', 'input-key', 'unknown-kind', 'targets-and-outputs', 'missing-member'):
            with self.subTest(failure=failure):
                invalid = design_document()
                call = invalid['processes'][0]['definition']
                if failure == 'empty':
                    call['outputs'] = {}
                elif failure == 'duplicate':
                    call['outputs']['second'] = deepcopy(call['outputs']['manufacturing'])
                elif failure == 'input-key':
                    call['outputs']['manufacturing']['key'] = 'design.rail'
                elif failure == 'unknown-kind':
                    call['outputs']['manufacturing']['kind'] = 'unspecified'
                elif failure == 'targets-and-outputs':
                    call['targets'] = {'stock': 'design.rail'}
                else:
                    call['processInput']['parts']['members']['itemKeys'] = ['missing']
                with self.assertRaises(ValueError):
                    validate_manufacturing_model(invalid)


if __name__ == '__main__':
    unittest.main()
