"""The product spatial choice executes the existing three-dimensional route."""
from copy import deepcopy
import unittest

from WindowManufacturingInputTests import (
    package, load, forbid_product_processing, forbid_product_callbacks,
)
from FoldedPostConnectionTests import allocate, full_plan
from icax_template_sdk.manufacturing import compose_manufacturing_model


FRAME = 'outer_frame.spatial.continuous'


class SpatialFoldProductControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.core = package('single_face_security_window')
        cls.contract = load('security_window_product_contract')

    def values(self, face='two', side='right', **changes):
        return {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side,
                'width': 1200., 'sideWidth': 600., 'height': 1800.,
                'accessDoorEnabled': False, 'frameManufacturingMode': 'spatial_v_notch',
                'outerFrameGrooveTool': 'system:edge-arc-groove', 'foldedPostJoint': 'tabs',
                'horizontalEndConnection': 'insert', 'verticalEndConnection': 'insert',
                'assemblyClearance': .1, **changes}

    def test_two_and_three_faces_both_sides_use_one_closed_spatial_stock_and_real_tools(self):
        for face in ('two', 'three'):
            for side in ('right', 'left'):
                with self.subTest(face=face, side=side):
                    values = self.values(face, side)
                    original = deepcopy(values)
                    # Product declarations remain independent of execution.
                    with forbid_product_processing():
                        design = self.core.display(values)
                        declaration = self.core.manufacturing(values)
                        echoed = self.contract._design(values, {'template': self.descriptor,
                            'geometryPurpose': 'display'}, self.core)
                    self.assertEqual(values, original)
                    self.assertEqual(echoed['parameters'], original)
                    self.assertEqual(set(echoed['parameters']), set(self.defaults))
                    self.assertNotIn('outerFrameConnection', echoed['parameters'])
                    self.assertNotIn('outerFrameFoldLayout', echoed['parameters'])
                    source = deepcopy((design, declaration))
                    with forbid_product_callbacks():
                        compiled = load('assembly_window_process').compile_window_assembly(
                            compose_manufacturing_model(declaration, design))
                        before = deepcopy(compiled)
                        allocation = allocate(compiled)
                        allocated = deepcopy(allocation)
                        plan = full_plan(allocation)
                    self.assertEqual((design, declaration), source)
                    self.assertEqual(compiled, before)
                    self.assertEqual(allocation, allocated)
                    frame = next(item for item in compiled['items'] if item['key'] == FRAME)
                    path = frame['manufacturingInput']['path']
                    self.assertTrue(path['closed'])
                    self.assertEqual(path['closure'], 'straight-mid-edge-seam')
                    self.assertEqual(path['segments'][-1]['to'], path['startVertex'])
                    vertices = {item['key']: item['point'] for item in path['vertices']}
                    directions = set()
                    for segment in path['segments']:
                        start, end = vertices[segment['from']], vertices[segment['to']]
                        directions.update(index for index in range(3)
                                          if abs(end[index] - start[index]) > 1.e-7)
                    self.assertEqual(directions, {0, 1, 2})
                    source_stocks = allocation['assemblySource']['stocks']
                    outer = [stock for stock in source_stocks if stock['id'].startswith('outer_frame.')]
                    self.assertEqual(len(outer), 2 if face == 'two' else 3)
                    self.assertEqual(sum(stock['id'] == FRAME for stock in outer), 1)
                    folds = [call for call in allocation['assemblySource']['instances']
                             if call['instanceId'].startswith(FRAME + '.fold.')]
                    self.assertEqual(len(folds), 6 if face == 'two' else 8)
                    self.assertGreater(len({call['processInput']['geometry']['rotation'] for call in folds}), 1)
                    executed = {operation['instanceId'] for operation in plan['operations']}
                    for fold in folds:
                        self.assertEqual(fold['templateId'], 'node-edge-arc-integrated')
                        self.assertEqual(fold['targets']['stock'], FRAME)
                        self.assertEqual(fold['processInput']['resources']['node-slot']['ref'],
                                         {'scope': 'system', 'id': 'edge-arc-groove'})
                        self.assertIn(fold['instanceId'], executed)
                    mapped = []
                    for item in allocation['model']['items']:
                        if item['key'].startswith('outer_frame.'):
                            profile = item['properties']['tubeDesigner.profile']
                            self.assertEqual((profile['width'], profile['depth'], profile['wallThickness']),
                                             (38., 38., 1.2))
                            mapped.extend(member['itemKey'] for member in
                                          item['properties']['manufacturing.sourceMembers'])
                    expected = {item['key'] for item in design['items']
                                if item['key'].startswith('outer_frame.')}
                    self.assertEqual(set(mapped), expected)
                    self.assertEqual(len(mapped), len(expected))

    def test_spatial_and_plane_choices_have_identical_display_documents_and_exact_input_echo(self):
        for face in ('two', 'three'):
            for side in ('right', 'left'):
                with self.subTest(face=face, side=side):
                    spatial = self.values(face, side, assemblyPlanningMode='external_templates')
                    plane = {**deepcopy(spatial), 'frameManufacturingMode': 'plane_v_notch'}
                    originals = deepcopy((spatial, plane))
                    with forbid_product_processing():
                        first = self.contract._design(spatial, {'template': self.descriptor,
                            'geometryPurpose': 'display'}, self.core)
                        second = self.contract._design(plane, {'template': self.descriptor,
                            'geometryPurpose': 'display'}, self.core)
                        self.assertEqual(self.core.display(spatial), self.core.display(plane))
                    self.assertEqual((spatial, plane), originals)
                    self.assertEqual(first['parameters'], spatial)
                    self.assertEqual(second['parameters'], plane)
                    for key in ('items', 'resources', 'outputs', 'relationships', 'annotations'):
                        self.assertEqual(first.get(key), second.get(key), key)

    def test_actual_spatial_route_retains_square_section_constraint_without_rewriting_material(self):
        values = self.values(frameDepth=30.)
        original = deepcopy(values)
        design, declaration = self.core.display(values), self.core.manufacturing(values)
        with forbid_product_callbacks(), self.assertRaisesRegex(ValueError, '宽深相等'):
            load('assembly_window_process').compile_window_assembly(
                compose_manufacturing_model(declaration, design))
        self.assertEqual(values, original)


if __name__ == '__main__':
    unittest.main()
