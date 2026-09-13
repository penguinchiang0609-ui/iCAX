import unittest
from WindowCatalogueTests import package


class GuardrailSideMountTests(unittest.TestCase):
    def test_families_layouts_and_elevations(self):
        for name in ("modular_guardrail", "modular_guardrail_glass-straight",
                     "modular_guardrail_cross-straight", "modular_guardrail_diamond-straight"):
            d, defaults, module = package(name)
            for mode in ("level", "continuous", "stepped"):
                for layout in ("straight", "left_l", "right_l", "u"):
                    with self.subTest(name=name, mode=mode, layout=layout):
                        p = dict(defaults, installation="side_plate", pathMode=mode, layout=layout,
                                 slopeAngle=30, slopeAngle2=-20, slopeAngle3=15)
                        built = module.build_layout(p)
                        posts = {post.key:post for post in built.posts}
                        tubes = {tube.key:tube for tube in built.tubes}
                        plates = [plate for plate in built.plates if plate.category == "plate.side_mount"]
                        self.assertEqual(len(plates), len(posts))
                        for plate in plates:
                            post = posts[plate.support_post]
                            self.assertEqual(plate.y_axis, (0,0,1))
                            self.assertAlmostEqual(plate.center[2], post.point[2]-p["sidePlateDrop"])
                            self.assertAlmostEqual(tubes[post.key].start[2], plate.center[2]-plate.height/2)
                            self.assertEqual(len(plate.holes),4)
                            for hole in plate.holes:
                                self.assertLess(abs(hole["x"])+hole["diameter"]/2,plate.width/2)
                                self.assertLess(abs(hole["y"])+hole["diameter"]/2,plate.height/2)
                        for purpose in ("display","manufacturing"):
                            result = module.generate(p, {"template":d, "geometryPurpose":purpose})
                            self.assertEqual(result["parameters"],p)
                            self.assertTrue(result["items"])

    def test_overlap_and_round_saddle(self):
        d, p, module = package("modular_guardrail_glass-straight")
        with self.assertRaisesRegex(ValueError, "重叠"):
            module.build_layout(dict(p,installation="side_plate",basePlateSize=2000))
        built = module.build_layout(dict(p,installation="side_plate",postProfileType="round"))
        for plate in built.plates:
            if plate.category == "plate.side_mount":
                self.assertEqual(plate.support_clips, (plate.support_post,))
