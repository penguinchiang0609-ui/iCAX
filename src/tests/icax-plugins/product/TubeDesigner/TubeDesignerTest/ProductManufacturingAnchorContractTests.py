"""Manufacturing connection anchors must name distinct participants of that joint."""
from copy import deepcopy
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import SHARED_ROOT, forbid_processing
from icax_template_sdk.manufacturing import compose_manufacturing_model, validate_manufacturing_model
import icax_template_worker as worker


class ProductManufacturingAnchorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _, parameters, template = package('single_face_security_window')
        before = deepcopy(parameters)
        cls.design = template.display(parameters)
        with forbid_processing():
            cls.recipe = template.manufacturing(parameters)
        assert parameters == before
        cls.participants = cls.recipe['connections'][0]['items']
        cls.outsider = next(item['key'] for item in cls.design['items']
                           if item['key'] not in cls.participants)

    def test_actual_joint_accepts_its_distinct_participant_anchors_without_mutation(self):
        recipe = deepcopy(self.recipe)
        recipe['connections'][0].setdefault('properties', {})['participantAnchors'] = [
            {'itemKey': key} for key in self.participants]
        frozen, design_before = deepcopy(recipe), deepcopy(self.design)
        with forbid_processing():
            validate_manufacturing_model(recipe)
            composed = compose_manufacturing_model(recipe, self.design)
        self.assertEqual(composed['schemaVersion'], 3)
        self.assertEqual(recipe, frozen)
        self.assertEqual(self.design, design_before)

    def test_invalid_anchors_are_rejected_before_downstream_processing_without_mutation(self):
        variants = {
            'existing-nonparticipant': [{'itemKey': self.outsider}],
            'duplicate-participant': [{'itemKey': self.participants[0]}] * 2,
            'non-array': {'itemKey': self.participants[0]},
            'non-object': [self.participants[0]],
            'missing-participant': [{}],
        }
        for name, anchors in variants.items():
            with self.subTest(name=name):
                recipe = deepcopy(self.recipe)
                recipe['connections'][0].setdefault('properties', {})['participantAnchors'] = anchors
                request = {'manufacturingDefinition': recipe, 'designModel': deepcopy(self.design),
                           'context': {'sharedRoot': str(SHARED_ROOT)}}
                frozen = deepcopy(request)
                with forbid_processing() as calls:
                    with self.assertRaises(ValueError):
                        validate_manufacturing_model(recipe)
                    with self.assertRaises(ValueError):
                        compose_manufacturing_model(recipe, request['designModel'])
                    with self.assertRaises(ValueError):
                        worker._execute_manufacturing(request)
                self.assertEqual(calls, [])
                self.assertEqual(request, frozen)


if __name__ == '__main__':
    unittest.main()
