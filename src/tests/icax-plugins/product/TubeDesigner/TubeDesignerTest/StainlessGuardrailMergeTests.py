import unittest
from pathlib import Path
from WindowCatalogueTests import package

class StainlessGuardrailMergeTests(unittest.TestCase):
    def test_stainless_configuration_in_all_handrail_families(self):
        for name in ("modular_guardrail","modular_guardrail_glass-straight",
                     "modular_guardrail_cross-straight","modular_guardrail_diamond-straight"):
            d,p,module=package(name)
            for key in ("handrailProfileType","postProfileType","railProfileType","infillProfileType"):
                self.assertIn("oval",[v["value"] for v in next(f for f in d["parameters"] if f["key"]==key)["choices"]])
            p.update(materialGrade="304不锈钢",handrailProfileType="oval",handrailWidth=60,handrailDepth=30,
                     postProfileType="round",postWidth=38,railProfileType="round",railWidth=25,
                     infillProfileType="round",infillWidth=19)
            for layout in ("straight","left_l","u"):
                for purpose in ("display","manufacturing"):
                    with self.subTest(template=name,layout=layout,purpose=purpose):
                        values=dict(p,layout=layout)
                        result=module.generate(values,{"template":d,"geometryPurpose":purpose})
                        self.assertEqual(result["parameters"],values)
                        tubes=[i for i in result["items"] if i.get("properties",{}).get("manufacturing.partKind")=="tube"]
                        self.assertTrue(tubes)
                        for item in tubes:self.assertEqual(item["properties"]["manufacturing.materialGrade"],"304不锈钢")
        self.assertFalse((Path(module.__file__).parent.parent/"stainless_steel_guardrail/template.json").exists())

if __name__=="__main__":unittest.main()
