"""One product outer-frame section serves horizontal and vertical members."""
from copy import deepcopy
import json
import unittest

from WindowManufacturingInputTests import package, load, forbid_product_processing
from icax_template_sdk.manufacturing import compose_manufacturing_model
from FoldedPostConnectionTests import allocate, full_plan


REMOVED_MATERIAL_FIELDS = (
    'outerFramePostMaterial', 'foldedPostProfileType', 'foldedPostWidth',
    'foldedPostDepth', 'foldedPostCornerRadius', 'foldedPostWallThickness',
)


def section_geometry(profile):
    # Display and manufacturing attach different cache/provenance metadata;
    # compare the actual section dimensions and complete contour definition.
    return {key: profile[key] for key in (
        'schema', 'width', 'depth', 'wallThickness', 'cornerRadius', 'contours',
    )}


def section_boundary(profile):
    # Allocated stock may begin the same closed contour at another edge.
    # Compare every line/arc's physical boundary, including arc midpoints.
    geometry = load('section_geometry')
    section = geometry.closed_shell_metrics(geometry.from_profile(profile))
    point = lambda values: tuple(round(value, 8) for value in values)
    loops = []
    for loop in section['contours']:
        edges = []
        for edge in loop['edges']:
            boundary = (edge['kind'], tuple(sorted((point(edge['start']), point(edge['end'])))))
            if edge['kind'] == 'circleArc':
                boundary += (point(edge['center']), round(edge['radius'], 8),
                             point(geometry.arc_point(edge, (edge['first'] + edge['last']) / 2)))
            edges.append(boundary)
        loops.append((loop['inner'], tuple(sorted(edges))))
    return tuple(sorted(loops))


class SecurityWindowMaterialSeparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.core = package('single_face_security_window')

    def values(self, **changes):
        return {**deepcopy(self.defaults), 'accessDoorEnabled': False, **changes}

    def test_product_contract_has_one_outer_frame_material(self):
        encoded = json.dumps(self.descriptor)
        for key in REMOVED_MATERIAL_FIELDS:
            self.assertNotIn(key, self.defaults)
            self.assertNotIn(key, encoded)
        self.assertNotIn('folded_post_profile', encoded)
        role = self.descriptor['extensions']['productDiagram']['profileRoles']['frame']
        self.assertEqual(set(role['parameters']), {
            'frameProfileType', 'frameWidth', 'frameDepth', 'frameCornerRadius', 'frameWallThickness',
        })
        outer_control = next(control for control in self.descriptor['extensions']['productControls']
                             if control['key'] == 'outerFrameConnection')
        self.assertNotIn('insert', {choice['value'] for choice in outer_control['choices']})
        self.assertIn('tabs', {choice['value'] for choice in outer_control['choices']})

    def test_all_faces_use_the_actual_outer_frame_section_for_every_member(self):
        for face in ('single', 'two', 'three', 'five'):
            for width, depth, wall, radius in ((38., 38., 1.2, 2.), (42., 46., 1.5, 3.)):
                with self.subTest(face=face, width=width, depth=depth):
                    values = self.values(faceType=face, frameWidth=width, frameDepth=depth,
                                         frameWallThickness=wall, frameCornerRadius=radius)
                    saved = deepcopy(values)
                    design = self.core.display(values)
                    expected = section_geometry(self.core._profile(values, 'frame').properties())
                    outer = [item for item in design['items'] if item['key'].startswith('outer_frame.')]
                    self.assertTrue(outer)
                    self.assertTrue(any('vertical' in item['key'] or '.left.' in item['key'] for item in outer))
                    self.assertTrue(any('horizontal' in item['key'] or '.top.' in item['key'] for item in outer))
                    for item in outer:
                        self.assertEqual(section_geometry(item['properties']['tubeDesigner.profile']), expected, item['key'])
                    self.assertEqual(values, saved)

    def test_manufacturing_routes_and_product_joints_keep_display_unchanged(self):
        for face in ('single', 'two', 'three', 'five'):
            baseline = self.values(faceType=face)
            with forbid_product_processing():
                expected = self.core.display(baseline)
            for mode in ('segment_weld', 'plane_v_notch', 'spatial_v_notch'):
                for joint in ('weld', 'tabs'):
                    with self.subTest(face=face, mode=mode, joint=joint):
                        values = {**baseline, 'frameManufacturingMode': mode, 'foldedPostJoint': joint}
                        saved = deepcopy(values)
                        with forbid_product_processing():
                            actual = self.core.display(values)
                        self.assertEqual(actual, expected)
                        self.assertEqual(values, saved)

    def test_same_section_tabs_build_real_end_and_socket_operations(self):
        for opening in (False, True):
            with self.subTest(opening=opening):
                values = self.values(foldedPostJoint='tabs', accessDoorEnabled=opening)
                saved = deepcopy(values)
                design = self.core.display(values)
                declaration = self.core.manufacturing(values)
                compiled = load('assembly_window_process').compile_window_assembly(
                    compose_manufacturing_model(declaration, design))
                allocation = allocate(compiled)
                post_calls = [call for call in allocation['assemblySource']['instances']
                              if call['templateId'] in ('tube-post-end', 'tube-post-aperture')]
                self.assertEqual(sum(call['templateId'] == 'tube-post-end' for call in post_calls), 4)
                self.assertEqual(sum(call['templateId'] == 'tube-post-aperture' for call in post_calls), 4)
                plan = full_plan(allocation)
                self.assertTrue({call['instanceId'] for call in post_calls} <=
                                {operation['instanceId'] for operation in plan['operations']})
                expected = self.core._profile(values, 'frame').properties()
                for item in allocation['model']['items']:
                    if item['key'].startswith('outer_frame.'):
                        actual = item['properties']['tubeDesigner.profile']
                        for key in ('width', 'depth', 'wallThickness', 'cornerRadius'):
                            self.assertEqual(actual[key], expected[key], item['key'] + ':' + key)
                        self.assertEqual(section_boundary(actual), section_boundary(expected), item['key'])
                self.assertEqual(values, saved)
                self.assertEqual(self.core.display(values), design)
                self.assertEqual(design, self.core.display({**values, 'foldedPostJoint': 'weld'}))

    def test_normalized_public_parameters_are_returned_unchanged_on_all_faces(self):
        for face in ('single', 'two', 'three', 'five'):
            values = self.values(faceType=face, foldedPostJoint='tabs', frameWidth=42., frameDepth=46.)
            original = deepcopy(values)
            result = load('security_window_product_contract')._design(
                values, {'template': self.descriptor, 'geometryPurpose': 'display'}, self.core)
            self.assertEqual(result['parameters'], original)
            self.assertEqual(values, original)
            self.assertEqual(set(result['parameters']), set(self.defaults))


if __name__ == '__main__':
    unittest.main()
