"""Video V12: equal frame/grid sections use the chosen real connection."""
from copy import deepcopy
import unittest
from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition


class CustomerEqualSectionConnectionTests(unittest.TestCase):
    def test_equal_25_frame_and_horizontal_bar_weld_or_tabs_all_faces(self):
        _, defaults, template = package('single_face_security_window')
        for face in ('single', 'two', 'three', 'five'):
            for joint in ('weld', 'tabs'):
                with self.subTest(face=face, joint=joint):
                    values = dict(defaults, faceType=face, accessDoorEnabled=False,
                        frameWidth=25., frameDepth=25., horizontalWidth=25., horizontalDepth=25.,
                        verticalWidth=16., verticalDepth=16., frameWallThickness=1.2,
                        horizontalWallThickness=1.2, verticalWallThickness=1.,
                        frameManufacturingMode='segment_weld', horizontalEndConnection=joint,
                        verticalEndConnection='weld')
                    saved = deepcopy(values)
                    design = template.display(values)
                    declaration = template.manufacturing(values)
                    executed = execute_definition(declaration, design=design)
                    self.assertEqual(values, saved)
                    self.assertTrue(executed['items'])
                    source = executed['extensions']['tubeDesigner.assemblyProcessSource']
                    ids = [v['templateId'] for v in source['instances']]
                    if joint == 'tabs':
                        self.assertIn('tube-post-aperture', ids)
                        self.assertIn('tube-post-end', ids)
                    else:
                        self.assertNotIn('tube-post-aperture', ids)

    def test_full_contour_insertion_of_equal_tubes_is_rejected_by_actual_joint(self):
        _, defaults, template = package('single_face_security_window')
        values = dict(defaults, accessDoorEnabled=False, frameWidth=25., frameDepth=25.,
                      horizontalWidth=25., horizontalDepth=25.,
                      horizontalEndConnection='insert', horizontalBranchReserve=5.)
        design = template.display(values)
        with self.assertRaisesRegex(ValueError, '内|插|容纳|放入|适用|尺寸|穿'):
            execute_definition(template.manufacturing(values), design=design)


if __name__ == '__main__':
    unittest.main()
