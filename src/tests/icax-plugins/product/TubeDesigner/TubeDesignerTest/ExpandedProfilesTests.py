"""Manufacturing-route regressions for the rebuilt market-profile library."""
import unittest
from ProfileDefinitionRuntimeTests import runtime
from ProfileFamilyTests import evaluate_model

class ExpandedProfiles(unittest.TestCase):
    def evaluate(self,key,**values):
        return evaluate_model(key,**values)

    def test_independent_welded_plate_thicknesses_change_the_section(self):
        for key in ("angle-welded","channel-welded","h-welded","t-welded"):
            with self.subTest(profile=key):
                a=self.evaluate(key,wallThickness=4,flangeThickness=8)
                b=self.evaluate(key,wallThickness=5,flangeThickness=10)
                self.assertNotEqual(a["contours"],b["contours"])
                self.assertEqual("polygon",a["contours"][0]["kind"])

    def test_rolled_roots_and_toes_are_exact_arcs(self):
        for key in ("angle-hot-unequal-thickness","channel-hot-parallel","i-hot-parallel","h-hot","t-split"):
            with self.subTest(profile=key):
                a=self.evaluate(key,rootRadius=3,toeRadius=1)
                self.assertTrue(any(s["kind"]=="arc" for s in a["contours"][0]["segments"]))

    def test_taper_and_independent_inner_axes_are_preserved(self):
        a=self.evaluate("i-hot-tapered",flangeSlope=5)
        b=self.evaluate("i-hot-tapered",flangeSlope=10)
        self.assertNotEqual(a["contours"],b["contours"])
        oval=self.evaluate("ellipse",innerWidth=51,innerDepth=29)
        self.assertEqual(51,oval["contours"][1]["width"])
        self.assertEqual(29,oval["contours"][1]["height"])

    def test_double_cell_rib_can_be_eccentric(self):
        a=self.evaluate("rect-double-cell",ribOffset=0)
        b=self.evaluate("rect-double-cell",ribOffset=5)
        self.assertEqual(a["contours"][0],b["contours"][0])
        self.assertNotEqual(a["contours"][1:],b["contours"][1:])
        self.assertEqual(3,b["contourCount"])

if __name__=="__main__":unittest.main()
