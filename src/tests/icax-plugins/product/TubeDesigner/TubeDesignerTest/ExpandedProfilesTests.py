"""Manufacturing-route regressions for the rebuilt market-profile library."""
import unittest
from ProfileDefinitionRuntimeTests import runtime
from ProfileFamilyTests import evaluate_model

class ExpandedProfiles(unittest.TestCase):
    def evaluate(self,key,**values):
        return evaluate_model(key,**values)

    def test_independent_welded_plate_thicknesses_change_the_section(self):
        cases=(
            ("t-section",{"useIndependentRadii":True,"rootRadius1":3,"rootRadius2":4},
             {"useIndependentRadii":True,"rootRadius1":5,"rootRadius2":2}),
        )
        for key,first,second in cases:
            with self.subTest(profile=key):
                a=self.evaluate(key,wallThickness=4,**first)
                b=self.evaluate(key,**second)
                self.assertNotEqual(a["contours"],b["contours"])
                self.assertIn(a["contours"][0]["kind"],{"path","polygon"})

    def test_rolled_roots_and_toes_are_exact_arcs(self):
        cases=(
            ("i-section",{"rootRadius":3}),
            ("t-section",{"rootRadius":3}),
        )
        for key,values in cases:
            with self.subTest(profile=key):
                a=self.evaluate(key,**values)
                self.assertTrue(any(s["kind"]=="arc" for s in a["contours"][0]["segments"]))

    def test_i_section_advanced_flange_offset_and_radii_are_preserved(self):
        a=self.evaluate("i-section",useIndependentFlangeWidths=True,
                        topFlangeWidth=96,bottomFlangeWidth=72,webOffset=4,
                        useIndependentRadii=True,rootRadius1=2,rootRadius2=3,
                        rootRadius3=4,rootRadius4=5)
        b=self.evaluate("i-section",useIndependentFlangeWidths=True,
                        topFlangeWidth=88,bottomFlangeWidth=76,webOffset=-3,
                        useIndependentRadii=True,rootRadius1=5,rootRadius2=4,
                        rootRadius3=3,rootRadius4=2)
        self.assertNotEqual(a["contours"],b["contours"])
        oval=self.evaluate("oval",model0InnerWidth=51,model0InnerDepth=29)
        inner=oval["contours"][1]["segments"][0]
        self.assertEqual(51,2*inner["majorRadius"])
        self.assertEqual(29,2*inner["minorRadius"])

    def test_double_cell_has_two_equal_square_cavities(self):
        a=self.evaluate("multi-cell",cellSize=30,wallThickness=3,ribThickness=4)
        b=self.evaluate("multi-cell",cellSize=35,wallThickness=3,ribThickness=4)
        self.assertNotEqual(a["contours"],b["contours"])
        self.assertEqual(3,a["contourCount"])
        for cavity in a["contours"][1:]:
            points=cavity["points"]
            width=max(point[0] for point in points)-min(point[0] for point in points)
            height=max(point[1] for point in points)-min(point[1] for point in points)
            self.assertEqual(30,width)
            self.assertEqual(30,height)
        self.assertEqual(3,b["contourCount"])

if __name__=="__main__":unittest.main()
