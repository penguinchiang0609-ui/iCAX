"""Five-face finished geometry is independent of stock and groove drafts."""
from copy import deepcopy
import unittest

from WindowCatalogueTests import package
from WindowManufacturingInputTests import forbid_product_processing


class FiveFaceDisplayProcessIndependenceTests(unittest.TestCase):
    def test_complete_display_stays_identical_when_outer_stock_route_changes(self):
        descriptor, defaults, product = package('single_face_security_window')
        for opening in (False, True):
            baseline_values = dict(defaults, faceType='five', accessDoorEnabled=opening,
                                   frameManufacturingMode='segment_weld')
            saved = deepcopy(baseline_values)
            with forbid_product_processing():
                baseline = product.display(baseline_values)
            self.assertEqual(baseline_values, saved)
            for mode in ('segment_weld', 'plane_v_notch'):
                for tool, bottom in (('system:v-notch-sharp', 'sharp'),
                                     ('system:v-notch-sharp', 'rounded'),
                                     ('system:edge-arc-groove', 'sharp')):
                    with self.subTest(opening=opening, mode=mode, tool=tool, bottom=bottom):
                        values = dict(baseline_values, frameManufacturingMode=mode,
                                      outerFrameGrooveTool=tool,
                                      outerFrameVGrooveBottomStrategy=bottom,
                                      outerFrameVGrooveRoundRadius=3.0,
                                      outerFrameBendKFactor=.62,
                                      outerFrameFoldBridge=.8,
                                      outerFrameVGrooveMaleFemale=False)
                        before = deepcopy(values)
                        with forbid_product_processing():
                            display = product.display(values)
                            document = product._generate_resource_document(values, {
                                'template': descriptor, 'geometryPurpose': 'display'})
                        self.assertEqual(values, before)
                        self.assertEqual(document['parameters'], before)
                        # Compare actual resources and placements, not only
                        # item counts or descriptive process flags.
                        self.assertEqual(display, baseline)

    def test_five_face_finished_frame_and_both_caps_are_real_geometry(self):
        _, defaults, product = package('single_face_security_window')
        values = dict(defaults, faceType='five', accessDoorEnabled=False,
                      width=1200., depth=600., height=1800.)
        with forbid_product_processing():
            display = product.display(values)
        members = {item['key']: item['properties']['assemblyFrame.member']
                   for item in display['items']}
        frame = {key: member for key, member in members.items()
                 if key.startswith('outer_frame.')}
        self.assertEqual(len(frame), 12)
        self.assertEqual(frame['outer_frame.vertical.0001']['start'], [-581., -581., 19.])
        self.assertEqual(frame['outer_frame.vertical.0001']['end'], [-581., -581., 1781.])
        # The back rails genuinely close the top and bottom frame loops.
        for back, side in (('outer_frame.back.0001', 'top'),
                           ('outer_frame.back.0002', 'bottom')):
            self.assertEqual(frame[back]['start'], frame[f'outer_frame.{side}.0001']['start'])
            self.assertEqual(frame[back]['end'], frame[f'outer_frame.{side}.0003']['end'])
        caps = [item for item in display['items'] if item['key'].startswith('cap_grid.')]
        self.assertTrue(caps)
        self.assertEqual({item['properties']['tubeDesigner.faceIndex'] for item in caps}, {4, 5})
        resources = {resource['key'] for resource in display['resources']}
        for item in display['items']:
            self.assertIn(item['geometry']['resource'], resources)

    def test_product_size_still_changes_the_finished_geometry(self):
        _, defaults, product = package('single_face_security_window')
        values = dict(defaults, faceType='five', accessDoorEnabled=False)
        before = deepcopy(values)
        with forbid_product_processing():
            initial = product.display(values)
            resized = product.display(dict(values, width=1400.))
        self.assertEqual(values, before)
        self.assertNotEqual(initial['resources'], resized['resources'])
        self.assertNotEqual(initial['items'], resized['items'])


if __name__ == '__main__':
    unittest.main()
