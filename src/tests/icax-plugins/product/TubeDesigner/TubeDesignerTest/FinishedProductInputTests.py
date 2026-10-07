"""Desired geometry is an immutable input to every assembly process."""
import copy
import json
from pathlib import Path
import runpy
import unittest

SOURCE = Path(__file__).resolve().parents[5]
RUNTIME = SOURCE / 'apps/tube-designer/templates/_shared/assembly_template_runtime.py'


class FinishedProductInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = runpy.run_path(str(RUNTIME))
        cls.products = cls.runtime['finished_products']

    def test_product_generation_does_not_read_a_process(self):
        generate = self.runtime['generate']
        original = generate.__globals__['_template_by_id']
        generate.__globals__['_template_by_id'] = lambda *_: self.fail('成品生成不应读取工艺')
        try:
            for shape in self.products.catalogue():
                product = self.products.create(shape['id'])
                result = generate({'action': 'preview-plan', 'finishedOnly': True,
                                   'finishedProduct': product}, {})
                self.assertEqual(result['finishedProduct'], product)
                self.assertNotIn('manufacturingParts', result)
                self.assertNotIn('templateId', result)
        finally:
            generate.__globals__['_template_by_id'] = original

    def test_all_process_files_and_public_catalogue_are_shape_free(self):
        catalogue = self.runtime['catalogue']()
        self.assertEqual(catalogue['errors'], [])
        raw = [json.loads(path.read_text(encoding='utf-8')) for path in
               (RUNTIME.parent.parent / 'assembly').glob('*/assembly.json')]
        required = {'bend', 'cross-through', 'end-side-tab-slot', 'flexible-slit-bend-integrated',
                    'four-end-end-end-end', 'insert-sleeve', 'mechanical-fastener',
                    'node-edge-arc-integrated', 'node-embedded-arc-integrated',
                    'node-v-notch-integrated', 'saddle-weld', 'segmented-bend',
                    'slot-bolt-adjustable', 't-contact-fit', 't-profile-insert', 'tab-slot-lock',
                    'through-bolt', 'two-end-end-angle', 'two-end-middle', 'weld-interface',
                    'wrap-a-over-b'}
        self.assertTrue(required.issubset({descriptor['id'] for descriptor in raw}))
        self.assertEqual({descriptor['id'] for descriptor in catalogue['assemblies']},
                         {descriptor['id'] for descriptor in raw
                          if not descriptor.get('catalogueHidden', False)})
        for descriptor in raw + catalogue['assemblies']:
            with self.subTest(template=descriptor['id']):
                self.assertIn('inputContract', descriptor)
                self.assertIn('exampleInput', descriptor)
                self.assertNotIn('productInput', descriptor)
                self.assertNotIn('designParts', descriptor['previewScene'])
                self.assertFalse(any(p.get('scope') == 'scene' for p in descriptor['parameters']))

    def test_same_l_product_becomes_one_or_two_blanks(self):
        product = self.products.create('l')
        product['parameters']['angle'] = 110
        product['spans']['armA']['length'] = 330
        before = copy.deepcopy(product)
        plans = [self.runtime['preview_plan'](name, finished_product=product)
                 for name in ('bend', 'two-end-end-angle', 'segmented-bend')]
        self.assertEqual([len(p['manufacturingParts']) for p in plans], [1, 2, 1])
        for plan in plans:
            self.assertEqual(plan['finishedProduct'], before)
            self.assertEqual(product, before)
        def geometry(plan):
            return [(p['request'], p['matrix']) for p in plan['designParts']]
        self.assertEqual(geometry(plans[0]), geometry(plans[1]))
        self.assertEqual(geometry(plans[0]), geometry(plans[2]))

    def test_all_processes_preserve_product_geometry(self):
        for path in (RUNTIME.parent.parent / 'assembly').glob('*/assembly.json'):
            descriptor = json.loads(path.read_text(encoding='utf-8'))
            with self.subTest(template=descriptor['id']):
                product = self.runtime['get_example_product'](descriptor['id'], {})['finishedProduct']
                if descriptor['id'] == 'insert-sleeve':
                    # A stepped round exterior is a user-owned product input.
                    for name, width in [('first', 30), ('second', 34.6)]:
                        product['spans'][name]['profileRef'] = {'scope': 'system', 'id': 'round'}
                        product['spans'][name]['parameters'] = {'width': width, 'wallThickness': 2,
                                                               'innerOffsetX': 0, 'innerOffsetY': 0}
                before = copy.deepcopy(product)
                plan = self.runtime['preview_plan'](descriptor['id'], finished_product=product)
                expected = {part['role']: part for part in self.runtime['finished_product_plan'](product)['designParts']}
                self.assertEqual(product, before)
                self.assertEqual(plan['finishedProduct'], before)
                for part in plan['designParts']:
                    span = expected[descriptor['exampleInput']['roles'][part['role']]]
                    self.assertEqual(part['request'], span['request'])
                    self.assertEqual(part['matrix'], span['matrix'])

    def test_process_limits_never_replace_product_values(self):
        product = self.products.create('l')
        product['parameters']['angle'] = 170
        before = copy.deepcopy(product)
        with self.assertRaisesRegex(ValueError, '不支持成品参数'):
            self.runtime['preview_plan']('two-end-end-angle', finished_product=product)
        self.assertEqual(product, before)
        self.assertEqual(self.runtime['finished_product_plan'](product)['finishedProduct'], before)
        self.assertEqual(self.runtime['preview_plan']('bend', finished_product=product)['finishedProduct'], before)

    def test_process_cannot_supply_or_mutate_product_fields(self):
        product = self.products.create('l')
        with self.assertRaisesRegex(ValueError, '工艺参数不能包含'):
            self.runtime['preview_plan']('bend', {'angle': 45}, finished_product=product)
        invalid = copy.deepcopy(product)
        invalid['parameters']['insertDepth'] = 120
        with self.assertRaisesRegex(ValueError, '工艺字段'):
            self.runtime['finished_product_plan'](invalid)
        invalid = copy.deepcopy(product)
        del invalid['spans']['armA']
        with self.assertRaisesRegex(ValueError, '不完整'):
            self.runtime['finished_product_plan'](invalid)


if __name__ == '__main__':
    unittest.main()
