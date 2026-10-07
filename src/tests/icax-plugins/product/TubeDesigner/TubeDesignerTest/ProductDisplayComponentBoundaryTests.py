"""Intrinsic shared components must not carry manufacturing declarations."""
from copy import deepcopy
import unittest

from WindowCatalogueTests import SRC, package
from ProductManufacturingDeclarationTests import forbid_processing
from icax_template_sdk.display import expand_display_model


MACHINING_FIELDS = {'cutPlanes', 'machiningFeatures', 'slotFeatures', 'stockState',
                    'stockInterval', 'stockLength', 'startReserve', 'endReserve',
                    'formingOrder', 'bendAllowance', 'sourceSpans'}


def machining_paths(value, path=''):
    if isinstance(value, dict):
        for key, child in value.items():
            if key in MACHINING_FIELDS:
                yield path + '.' + key
            yield from machining_paths(child, path + '.' + key)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from machining_paths(child, path + '[' + str(index) + ']')


class ProductDisplayComponentBoundaryTests(unittest.TestCase):
    def test_nested_machining_fields_are_rejected_even_without_manufacturing_prefix(self):
        _, values, template = package('assembly_frame_lt')
        design = template.display(values)
        for field in ('cutPlanes', 'machiningFeatures', 'slotFeatures'):
            with self.subTest(field=field):
                document = deepcopy(design)
                document['items'][0]['properties']['customComponent'] = {field: []}
                saved = deepcopy(document)
                with self.assertRaises(ValueError):
                    expand_display_model(document)
                self.assertEqual(document, saved)

    def test_all_active_default_display_components_have_no_machining_or_stock_fields(self):
        root = SRC / 'apps/tube-designer/templates/product'
        directories = [entry for entry in sorted(root.iterdir())
                       if not entry.name.startswith('_') and (entry / 'template.json').exists()]
        self.assertEqual(len(directories), 13)
        for directory in directories:
            with self.subTest(template=directory.name):
                _, values, template = package(directory.name)
                before = deepcopy(values)
                document = template.display(values)
                self.assertEqual(values, before)
                leaked = [item['key'] + path for item in document['items']
                          for path in machining_paths(item.get('properties', {}))]
                self.assertEqual(leaked, [])

    @unittest.skip("Deferred product reference; no current active product generation")
    def test_louver_slot_and_end_requirements_are_declared_only_by_manufacturing(self):
        _, defaults, template = package('louver_window')
        values = dict(defaults, bladeConnection='through_insert', supportMode='through',
                      middlePostCount=2, middleBeamCount=2, bladeDirectionAngle=30, bladePitch=100)
        before = deepcopy(values)
        display = template.display(values)
        with forbid_processing():
            manufacturing = template.manufacturing(values)
        self.assertEqual(values, before)
        for item in display['items']:
            component = item.get('properties', {}).get('louver.part', {})
            self.assertNotIn('endReceivers', component)
            self.assertNotIn('miter', component)
            self.assertEqual(list(machining_paths(component)), [])
        identifiers = {call['definition']['templateId'] for call in manufacturing['processes']}
        self.assertIn('structural-planar-trim', identifiers)
        self.assertIn('structural-profile-pocket', identifiers)
        self.assertTrue(manufacturing['connections'])

    @unittest.skip("Deferred product reference; no current active product generation")
    def test_louver_planning_mode_preserves_the_entire_shared_display(self):
        _, defaults, template = package('louver_window')
        for changes in ({}, {'middlePostCount': 2, 'middleBeamCount': 1},
                        {'bladeDirectionAngle': 90, 'frameWidth': 60, 'frameDepth': 40}):
            with self.subTest(changes=changes):
                first = dict(defaults, **changes, assemblyPlanningMode='legacy_processed')
                second = dict(first, assemblyPlanningMode='external_templates')
                first_before, second_before = deepcopy(first), deepcopy(second)
                self.assertEqual(template.display(first), template.display(second))
                self.assertEqual(first, first_before)
                self.assertEqual(second, second_before)


if __name__ == '__main__':
    unittest.main()
