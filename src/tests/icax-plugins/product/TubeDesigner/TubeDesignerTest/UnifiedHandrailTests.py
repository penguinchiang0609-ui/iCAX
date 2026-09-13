import math
import unittest
from WindowCatalogueTests import package

class UnifiedHandrailTests(unittest.TestCase):
    def test_continuous_rails_and_extensions(self):
        for name in ("modular_guardrail","modular_guardrail_glass-straight","modular_guardrail_cross-straight","modular_guardrail_diamond-straight"):
            d, defaults, module=package(name)
            for mode in ("level","continuous"):
                for shape in ("straight","u"):
                    with self.subTest(name=name,mode=mode,shape=shape):
                        p=dict(defaults,pathMode=mode,layout=shape,handrailMode="continuous",startExtension=100,finishExtension=200,slopeAngle=30,slopeAngle2=-20,slopeAngle3=15)
                        built=module.build_layout(p)
                        caps=[t for t in built.tubes if t.category=="guardrail.handrail"]
                        self.assertEqual(len(caps),1 if shape=="straight" else 3)
                        for i,cap in enumerate(caps):
                            s=built.elevation_segments[i]
                            expected=s["length"]/s["cos"]+(100 if i==0 else 0)+(200 if i==len(caps)-1 else 0)
                            self.assertAlmostEqual(cap.length,expected)
                            self.assertFalse(cap.clips)
                        for post in (t for t in built.tubes if t.category=="guardrail.post"):
                            self.assertTrue(post.clips)
                            self.assertTrue(post.keep_volume)
                        for purpose in ("display","manufacturing"):
                            doc=module.generate(p,{"template":d,"geometryPurpose":purpose})
                            self.assertEqual(doc["parameters"],p)

    def test_horizontal_count_and_retained_step_option(self):
        d,p,module=package("modular_guardrail")
        for mode in ("level","continuous","stepped"):
            values=dict(p,pathMode=mode,barOrientation="horizontal",horizontalRailCount=4,handrailMode="continuous")
            built=module.build_layout(values)
            self.assertEqual(sum(t.category=="guardrail.horizontal_bar" for t in built.tubes),4*len(built.bays))
            self.assertFalse(any(t.category=="guardrail.vertical_bar" for t in built.tubes))
            self.assertEqual(module.generate(values,{"template":d,"geometryPurpose":"manufacturing"})["parameters"],values)
