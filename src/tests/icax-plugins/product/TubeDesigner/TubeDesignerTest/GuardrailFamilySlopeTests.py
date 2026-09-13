import math
import unittest
from WindowCatalogueTests import package


class GuardrailFamilySlopeTests(unittest.TestCase):
    def test_explicit_bays_on_tread_centres(self):
        for name in ("modular_guardrail", "modular_guardrail_glass-straight",
                     "modular_guardrail_cross-straight", "modular_guardrail_diamond-straight"):
            d, defaults, module = package(name)
            p = dict(defaults, pathMode="continuous", elevationSource="treads",
                     treadCount1=10, treadGoing=280, sideBayCount1=3)
            built = module.build_layout(p)
            self.assertEqual(len(built.bays), 3)
            for post in built.posts:
                self.assertAlmostEqual(post.point[0]/280, round(post.point[0]/280))
            with self.assertRaises(ValueError):
                module.build_layout(dict(p, sideBayCount1=11))

    def test_wall_pickets_keep_holes_and_tips(self):
        d, defaults, module = package("modular_guardrail")
        for mode in ("continuous", "stepped"):
            p = dict(defaults, pathMode=mode, guardrailUse="wall", slopeAngle=30, spearTipEnabled=True)
            built = module.build_layout(p)
            bars = {t.key:t for t in built.tubes if t.category == "guardrail.vertical_bar"}
            self.assertTrue(any(t.hole_tools for t in built.tubes))
            for tip in (c for c in built.components if c.category == "accessory.spear_tip"):
                self.assertAlmostEqual(tip.origin[2], bars[tip.key.removesuffix(".tip")].end[2])
            result = module.generate(p, {"template":d, "geometryPurpose":"manufacturing"})
            self.assertEqual(result["parameters"], p)

    def test_all_families_piecewise_slopes(self):
        for name in ("modular_guardrail", "modular_guardrail_glass-straight",
                     "modular_guardrail_cross-straight", "modular_guardrail_diamond-straight"):
            d, defaults, module = package(name)
            for mode in ("continuous", "stepped"):
                for layout in ("straight", "left_l", "right_l", "u"):
                    for angle in (-30, 0, 30):
                        with self.subTest(name=name, mode=mode, layout=layout, angle=angle):
                            p = dict(defaults, pathMode=mode, layout=layout, slopeAngle=angle,
                                     slopeAngle2=-angle, slopeAngle3=angle, sideBayCount1=2, sideBayCount2=3, sideBayCount3=2)
                            built = module.build_layout(p)
                            sides = 1 if layout == "straight" else 3 if layout == "u" else 2
                            self.assertEqual(len(built.bays), [2, 5, 7][sides-1])
                            for tube in built.tubes:
                                axis = [(b-a)/tube.length for a,b in zip(tube.start,tube.end)]
                                self.assertAlmostEqual(sum(a*b for a,b in zip(axis,tube.x_axis)), 0)
                                self.assertAlmostEqual(sum(a*b for a,b in zip(axis,tube.y_axis)), 0)
                                if tube.category == "guardrail.post":
                                    self.assertEqual(tube.start[:2], tube.end[:2])
                            panels = [plate for plate in built.plates if plate.key.endswith(".panel")]
                            for panel in panels:
                                slope = built.elevation_segments[int(panel.group.split(".")[1])-1]["slope"] if mode == "continuous" else 0
                                a,b,c,e = panel.outline
                                self.assertAlmostEqual((b[1]-a[1])/(b[0]-a[0]), slope)
                                self.assertAlmostEqual(a[0],e[0])
                                self.assertAlmostEqual(b[0],c[0])
                            for purpose in ("display", "manufacturing"):
                                result = module.generate(p, {"template": d, "geometryPurpose": purpose})
                                self.assertTrue(result["items"])
                                self.assertEqual(result["parameters"], p)
