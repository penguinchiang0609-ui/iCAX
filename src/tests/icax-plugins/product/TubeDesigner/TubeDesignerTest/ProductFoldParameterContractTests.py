"""New product fold drafts stay unchanged and do not define display geometry."""
from copy import deepcopy
import unittest
from WindowManufacturingInputTests import package, load, forbid_product_processing


class ProductFoldParameterContractTests(unittest.TestCase):
    def test_all_face_wrappers_return_exact_host_input_and_stable_display(self):
        descriptor, defaults, core = package('single_face_security_window')
        contract = load('security_window_product_contract')
        self.assertEqual(descriptor['version'], core.TEMPLATE_VERSION)
        for face in ('single', 'two', 'three', 'five'):
            for opening in (False, True):
                baseline = {**defaults, 'faceType': face, 'accessDoorEnabled': opening,
                            'frameManufacturingMode': 'plane_v_notch',
                            'doorFrameJoinType': 'v_groove_90:tool_library',
                            'doorLeafFrameJoinType': 'v_groove_90:tool_library'}
                with forbid_product_processing():
                    original_display = core.display(baseline)
                for factor, male in ((0, True), (.57, False), (1, True)):
                    with self.subTest(face=face, opening=opening, factor=factor, male=male):
                        values = deepcopy(baseline)
                        for index, prefix in enumerate(('outerFrame', 'doorFrame', 'doorLeafFrame')):
                            values[prefix+'BendKFactor'] = max(0, factor - .1*index)
                            values[prefix+'FoldBridge'] = .7 + .2*index
                            values[prefix+'VGrooveMaleFemale'] = male if index != 1 else not male
                            values[prefix+'VGrooveBottomStrategy'] = 'rounded' if index != 1 else 'sharp'
                            values[prefix+'VGrooveRoundRadius'] = 2.0 + .2*index
                        saved = deepcopy(values)
                        with forbid_product_processing():
                            display = core.display(values)
                            declarations = core.manufacturing(values)
                            raw = contract.generate(values, {'template': descriptor, 'geometryPurpose': 'display'}, core)
                        self.assertEqual(values, saved)
                        self.assertEqual(raw['parameters'], saved, 'No internally derived or renamed host fields')
                        self.assertEqual(display, original_display, 'Manufacturing drafts cannot alter display geometry')
                        self.assertTrue(declarations['processes'])
                        for index, prefix in enumerate(('outerFrame', 'doorFrame', 'doorLeafFrame')):
                            self.assertEqual(values[prefix+'BendKFactor'], max(0, factor - .1*index))
                            self.assertEqual(values[prefix+'FoldBridge'], .7 + .2*index)
                            self.assertEqual(values[prefix+'VGrooveMaleFemale'], male if index != 1 else not male)
                            self.assertEqual(values[prefix+'VGrooveBottomStrategy'], 'rounded' if index != 1 else 'sharp')
                            self.assertEqual(values[prefix+'VGrooveRoundRadius'], 2.0 + .2*index)


if __name__ == '__main__':
    unittest.main()
