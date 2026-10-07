"""Current product declares grouping after complete native machining validation."""
from copy import deepcopy
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk.manufacturing import validate_manufacturing_model


class ProductIdenticalManufacturingDeclarationTests(unittest.TestCase):
    def test_all_current_faces_declare_grouping_without_mutating_host_parameters(self):
        _, defaults, template = package('single_face_security_window')
        for face in ('single', 'two', 'three', 'five'):
            with self.subTest(face=face):
                values = dict(defaults, faceType=face, accessDoorEnabled=True)
                before = deepcopy(values)
                with forbid_processing() as calls:
                    declaration = template.manufacturing(values)
                validate_manufacturing_model(declaration)
                self.assertEqual(calls, [])
                self.assertEqual(values, before)
                self.assertNotIn('mergeIdenticalParts', values)
                self.assertTrue(declaration['processes'][0]['definition']['processInput']['geometry']['mergeIdenticalParts'])

    def test_executor_preserves_all_physical_stock_recipes_and_source_members(self):
        _, defaults, template = package('single_face_security_window')
        values = dict(defaults, faceType='single', width=1200., height=1800., accessDoorEnabled=True,
                      frameManufacturingMode='segment_weld', frameJoinType='miter_45',
                      horizontalEndConnection='tabs', verticalEndConnection='tabs')
        before = deepcopy(values)
        declaration = template.manufacturing(values)
        saved = deepcopy(declaration)
        executed = execute_definition(declaration, design=template.display(values))
        self.assertEqual(values, before)
        self.assertEqual(declaration, saved)
        extensions = executed['extensions']
        self.assertEqual(extensions['tubeDesigner.identicalManufacturingPartsVersion'], 1)
        stocks = extensions['tubeDesigner.assemblyProcessSource']['stocks']
        self.assertEqual(len(stocks), 41)
        self.assertEqual(len({stock['id'] for stock in stocks}), 41)
        source_members = [mapping['itemKey'] for item in executed['items']
                          for mapping in item['properties'].get('manufacturing.sourceMembers', [])]
        self.assertEqual(len(source_members), 41)
        self.assertEqual(len(set(source_members)), 41)
        self.assertTrue(extensions['tubeDesigner.assemblyProcessSource']['instances'])


if __name__ == '__main__':
    unittest.main()
