"""The assembled round joint must show the same outer-envelope contact as the part."""
from copy import deepcopy
import unittest
from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition
from icax_template_sdk.display import expand_display_model
from icax_template_sdk import expand_resource_model


class CustomerRoundGuardrailContactTests(unittest.TestCase):
    def test_round_contact_design_and_manufacturing_preserve_member_identities(self):
        _, values, template = package('modular_guardrail')
        for prefix, size in [('handrail',50),('post',40),('rail',30),('infill',20)]:
            values[prefix+'ProfileType']='round'
            values[prefix+'Width']=size
        saved=deepcopy(values)
        design=template.display(values)
        expanded=expand_resource_model(expand_display_model(design))
        nodes={n['key']:n for n in expanded['geometry']}
        post=next(item for item in expanded['items'] if item['key']=='post.0001')
        root=nodes[post['representations']['result']]
        self.assertEqual(root['operator'],'boolean')
        self.assertEqual(root['arguments']['operation'],'subtract')
        executed=execute_definition(template.manufacturing(values),design=design)
        self.assertEqual({item['key'] for item in executed['items']}, {item['key'] for item in expanded['items']})
        self.assertEqual(values,saved)
        self.assertEqual(len(expanded['items']),32)


if __name__ == '__main__': unittest.main()
