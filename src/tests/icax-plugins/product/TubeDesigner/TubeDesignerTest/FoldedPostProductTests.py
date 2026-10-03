"""Independent folded-corner post sections are intrinsic product facts."""
from copy import deepcopy
import unittest

from WindowManufacturingInputTests import load, package, forbid_product_processing


def posts(model):
    return {item['key']: item['properties'] for item in model['items']
            if item['key'].startswith('outer_frame.vertical.')}


class FoldedPostProductTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.core = package('single_face_security_window')

    def values(self, **changes):
        return {**deepcopy(self.defaults), 'accessDoorEnabled': False, **changes}

    def test_only_independent_corner_posts_use_the_selected_section(self):
        for face, mode, widths in (('two', 'plane_v_notch', [38, 30, 38]),
                                   ('three', 'plane_v_notch', [38, 30, 30, 38]),
                                   ('five', 'plane_v_notch', [30, 30, 30, 30]),
                                   ('two', 'spatial_v_notch', [38, 30, 38]),
                                   ('three', 'spatial_v_notch', [38, 30, 30, 38])):
            for joint in ('insert', 'tabs'):
                with self.subTest(face=face, mode=mode, joint=joint):
                    values = self.values(faceType=face, frameManufacturingMode=mode, foldedPostJoint=joint)
                    original = deepcopy(values)
                    with forbid_product_processing():
                        design = self.core.display(values)
                        declaration = self.core.manufacturing(values)
                    self.assertEqual(values, original)
                    actual = posts(design)
                    self.assertEqual([actual[key]['tubeDesigner.profile']['width'] for key in sorted(actual)], widths)
                    for post in actual.values():
                        member = post['assemblyFrame.member']
                        self.assertEqual(member['start'][2], 19.)
                        self.assertEqual(member['end'][2], values['height'] - 19.)
                    choices = declaration['processes'][0]['definition']['parameters']
                    self.assertEqual(choices['foldedPostJoint'], joint)
                    for relation in declaration['connections']:
                        if relation['key'].startswith('outer_frame.corner.'):
                            anchors = relation['properties']['participantAnchors']
                            self.assertTrue(all(anchor['centerlinePoint'] == anchors[0]['centerlinePoint']
                                                for anchor in anchors))

    def test_weld_and_inactive_routes_keep_legacy_geometry_and_ignore_section_drafts(self):
        invalid_section = {'foldedPostWidth': 1., 'foldedPostDepth': 1.,
                           'foldedPostWallThickness': 50., 'foldedPostCornerRadius': 50.}
        invalid_process = {'foldedPostInsertDepth': 0., 'foldedPostTabWidth': 0.,
                           'foldedPostTabLength': 0., 'foldedPostFitGap': 100.,
                           'foldedPostSideClearance': 100.}
        for face, mode, joint in (('three', 'plane_v_notch', 'weld'),
                                  ('three', 'segment_weld', 'weld'),
                                  ('three', 'spatial_v_notch', 'weld'),
                                  ('single', 'plane_v_notch', 'insert')):
            with self.subTest(face=face, mode=mode, joint=joint):
                values = self.values(faceType=face, frameManufacturingMode=mode, foldedPostJoint=joint)
                baseline = self.core.display(values)
                drafted = {**values, **invalid_section, **invalid_process}
                original = deepcopy(drafted)
                with forbid_product_processing():
                    result = self.core.display(drafted)
                    declaration = self.core.manufacturing(drafted)
                self.assertEqual(result, baseline)
                self.assertEqual(drafted, original)
                self.assertEqual(declaration['processes'][0]['definition']['parameters']['foldedPostJoint'], 'weld')

    def test_public_input_and_returned_parameters_keep_the_complete_drafts(self):
        contract = load('security_window_product_contract')
        for mode in ('segment_weld', 'plane_v_notch', 'spatial_v_notch'):
            for joint in ('weld', 'insert', 'tabs'):
                with self.subTest(mode=mode, joint=joint):
                    values = self.values(faceType='three', frameManufacturingMode=mode, foldedPostJoint=joint,
                                         foldedPostInsertDepth=13., foldedPostTabWidth=9.)
                    original = deepcopy(values)
                    result = contract._design(values, {'template': self.descriptor, 'geometryPurpose': 'display'}, self.core)
                    self.assertEqual(values, original)
                    self.assertEqual(result['parameters'], original)
                    self.assertNotIn('_foldedPostSectionEnabled', result['parameters'])

    def test_selected_profile_resource_preserves_authoritative_contours_and_frame_datum(self):
        def rectangle(width, depth):
            return {'kind': 'polygon', 'points': [[-width/2, -depth/2], [width/2, -depth/2],
                    [width/2, depth/2], [-width/2, depth/2]]}
        def snapshot(width, depth, role):
            return {'schema': 'icax.imported-tube-profile', 'schemaVersion': 1,
                    'kind': 'fixed-section', 'profileForm': 'fixed', 'sectionKind': 'rect',
                    'name': role, 'specification': role, 'contentDigest': 'folded-post-' + role,
                    'width': width, 'depth': depth, 'wallThickness': 1.,
                    'contours': [rectangle(width, depth), rectangle(width-2., depth-2.)]}
        from icax_template_sdk.profile_constraints import _profile_validation_scope
        frame, folded = snapshot(42., 42., 'frame'), snapshot(28., 30., 'post')
        values = self.values(faceType='three', frameManufacturingMode='plane_v_notch', foldedPostJoint='insert',
                             tubeDesignerProfileOverrides={'frame': frame, 'foldedPost': folded})
        original = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            result = self.core.display(values)
        actual = posts(result)
        self.assertEqual([actual[key]['tubeDesigner.profile']['width'] for key in sorted(actual)], [42., 28., 28., 42.])
        self.assertEqual(actual['outer_frame.vertical.0002']['tubeDesigner.profile']['contours'], folded['contours'])
        self.assertTrue(all(post['assemblyFrame.member']['start'][2] == 21. for post in actual.values()))
        self.assertEqual(values, original)


if __name__ == '__main__':
    unittest.main()
