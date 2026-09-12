"""Survey-backed forward library: provenance, dimensions and manufacturing differences."""
import importlib.util
import json
from pathlib import Path
import unittest
from ProfileFamilyTests import evaluate_model

ROOT=next(p for p in Path(__file__).resolve().parents if (p/"src/apps").is_dir())
SHARED=ROOT/"src/apps/tube-designer/templates/_shared"
spec=importlib.util.spec_from_file_location("market_runtime",SHARED/"profile_package_runtime.py")
runtime=importlib.util.module_from_spec(spec);spec.loader.exec_module(runtime)

class MarketLibrary(unittest.TestCase):
    def evaluate(self,profile_id,**values):
        return evaluate_model(profile_id,**values)

    def test_every_active_template_has_a_survey_reference_and_complete_dependencies(self):
        packages=runtime.generate({"action":"list-system"},{})["systemProfiles"]
        self.assertEqual(24,len(packages))
        for p in packages:
            with self.subTest(profile=p.get("name")):
                self.assertNotIn("error",p)
                d=p["descriptor"]
                self.assertTrue(d["provenance"]["surveyIds"])
                self.assertEqual("not-certified",d["provenance"]["standardConformance"])
                self.assertNotIn("recognition",d)
                self.assertIn("geometry.py",p["resources"])
                actual=self.evaluate(d["id"])
                self.assertEqual(d["provenance"],actual["provenance"])
                self.assertEqual(d["manufacturing"],actual["manufacturing"])

    def test_all_104_survey_rows_are_accounted_for_without_fake_templates(self):
        coverage=json.loads((ROOT/"doc/Apps/管型模板库重建_2026-09-12.json").read_text(encoding="utf-8"))
        self.assertEqual(list(range(1,105)),[row["surveyId"] for row in coverage["entries"]])
        for row in coverage["entries"]:
            if row["surveyId"] in (7,9,25,32,33,34,83,84):
                self.assertFalse(row["templateIds"])
            if row["surveyId"]>=99:self.assertEqual("requires-3d-model",row["status"])

    def test_rectangle_preserves_independent_corners_and_eccentric_bore(self):
        result=self.evaluate("rect",width=80,depth=50,innerWidth=68,innerDepth=38,
                             innerOffsetX=1,innerOffsetY=-1,outerRadii="3,4,5,6",innerRadii="1,2,3,4")
        self.assertEqual(2,len(result["contours"]))
        self.assertNotEqual(result["contours"][0],result["contours"][1])
        self.assertEqual(4,len([s for s in result["contours"][0]["segments"] if s["kind"]=="arc"]))

    def test_cold_rolled_and_welded_are_geometrically_distinct(self):
        hot=self.evaluate("angle-hot-unequal-thickness")
        cold=self.evaluate("angle-cold-unequal")
        welded=self.evaluate("angle-welded")
        self.assertNotEqual(hot["contours"],cold["contours"])
        self.assertEqual("path",hot["contours"][0]["kind"])
        self.assertEqual("polygon",welded["contours"][0]["kind"])
        self.assertNotEqual(self.evaluate("channel-hot-tapered")["contours"],
                            self.evaluate("channel-hot-parallel")["contours"])

    def test_open_seam_and_solid_are_not_hollow_tubes(self):
        self.assertEqual(1,self.evaluate("round-open-seam")["contourCount"])
        self.assertFalse(self.evaluate("round-bar")["hollow"])
        self.assertEqual(3,self.evaluate("rect-double-cell")["contourCount"])

    def test_invalid_extents_and_overlapping_bends_are_rejected(self):
        for name,p in [("rect",{"innerWidth":1000}), ("round",{"wallThickness":100}),
                       ("channel-cold-u",{"bendRadius":1000}), ("h-hot",{"rootRadius":1000})]:
            with self.subTest(profile=name),self.assertRaises(ValueError):self.evaluate(name,**p)

    def test_product_selectors_reference_only_active_templates(self):
        ids={p.name for p in (ROOT/"src/apps/tube-designer/templates/profile").iterdir() if p.is_dir()}
        for file in (ROOT/"src/apps/tube-designer/templates/product").rglob("template.json"):
            data=json.loads(file.read_text(encoding="utf-8-sig"))
            choices={p["key"]:{o["value"] for o in p.get("choices",[]) + p.get("options",[])}
                     for p in data.get("parameters",[]) if p["key"].endswith("ProfileType")}
            def check_conditions(node):
                if isinstance(node,dict):
                    key=node.get("parameter")
                    if key in choices and "value" in node:
                        self.assertIn(node["value"],choices[key],str(file))
                    for value in node.values():check_conditions(value)
                elif isinstance(node,list):
                    for value in node:check_conditions(value)
            check_conditions(data)
            for p in data.get("parameters",[]):
                if p["key"].endswith("ProfileType"):
                    self.assertIn(p["defaultValue"],ids,str(file))
                    for o in p.get("choices",[]) + p.get("options",[]):
                        self.assertIn(o["value"],ids,str(file))

if __name__=="__main__":unittest.main()
