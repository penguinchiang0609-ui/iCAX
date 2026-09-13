import math
import unittest
from WindowCatalogueTests import package

class GuardrailMiddlePostTests(unittest.TestCase):
    def test_odd_bays_keep_middle_post_and_count(self):
        for name in ("modular_guardrail", "modular_guardrail_glass-straight",
                     "modular_guardrail_cross-straight", "modular_guardrail_diamond-straight"):
            d, defaults, module = package(name)
            for count in (2,3,5):
                for mode in ("level","continuous","stepped"):
                    with self.subTest(name=name,count=count,mode=mode):
                        p = dict(defaults,layout="u",largePostMode="middle",sideBayCount1=count,
                                 sideBayCount2=2,sideBayCount3=3,pathMode=mode,slopeAngle=30,slopeAngle2=-20)
                        built = module.build_layout(p)
                        self.assertEqual(len(built.bays),count+5)
                        self.assertEqual(sum(post.large for post in built.posts),3)
                        for i in range(3):
                            bays = [bay for bay in built.bays if bay.key.startswith(f"segment.{i+1}.")]
                            midpoint = tuple((bays[0].left.point[j]+bays[-1].right.point[j])/2 for j in range(2))
                            middle = next(post for post in built.posts if post.large and math.dist(post.point[:2],midpoint)<1e-6)
                            self.assertTrue(middle.large)
                        for purpose in ("display","manufacturing"):
                            document = module.generate(p,{"template":d,"geometryPurpose":purpose})
                            self.assertEqual(document["parameters"],p)
                            self.assertTrue(document["items"])

    def test_treads_keep_odd_count_and_interior_middle(self):
        d,p,module=package("modular_guardrail_diamond-straight")
        for steps in (5,10,11):
            values=dict(p,pathMode="continuous",elevationSource="treads",treadCount1=steps,
                        sideBayCount1=3,largePostMode="middle")
            built=module.build_layout(values)
            self.assertEqual(len(built.bays),3)
            self.assertEqual(sum(post.large for post in built.posts),1)
            for post in built.posts:
                self.assertAlmostEqual(post.point[0]/p["treadGoing"],round(post.point[0]/p["treadGoing"]))
