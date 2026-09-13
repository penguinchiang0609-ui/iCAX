from copy import deepcopy
import unittest
from WindowCatalogueTests import package

class DecorativeDoorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.d,cls.defaults,cls.module=package("decorative_door")
    def build(self,purpose="display",**values):
        p=dict(self.defaults,**values); before=deepcopy(p)
        r=self.module.generate(p,{"template":self.d,"geometryPurpose":purpose})
        self.assertEqual(p,before);self.assertEqual(r["parameters"],before)
        self.assertTrue(r["items"])
        self.assertFalse(r["extensions"]["doorDecoration"]["ncReady"])
        return r
    def test_patterns_structures_compositions_and_layouts(self):
        for pattern in ("lines","diamond","octagon","round_scene","panels","glass_lattice"):
            for door in ("single","double","mother","multi"):
                for composition in ("repeat","mirror","continuous"):
                    for layout in ("full","center_band","offset_band","upper","lower","two_blocks"):
                        for purpose in ("display","manufacturing"):
                            with self.subTest(pattern=pattern,door=door,composition=composition,layout=layout,purpose=purpose):
                                self.build(purpose,pattern=pattern,doorType=door,composition=composition,layout=layout,
                                           width=3000,leafCount=3,columns=1,rows=2)
    def test_inactive_values_do_not_change_geometry(self):
        base=dict(pattern="diamond",regionProcess="through",doorType="single")
        first=self.build(**base)
        second=self.build(**base,cutDepth=99,minSkin=99,machiningSide="both",leafGap=20,
                          motherRatio=.9,leafCount=6,composition="continuous",meetingBorder=200,
                          vAngle=10,trimWidth=100,glassThickness=99)
        self.assertEqual(first["geometry"],second["geometry"])
    def test_remaining_thickness_and_v_coupling(self):
        r=self.build(pattern="lines",lineTool="v",cutDepth=7,machiningSide="both")
        self.assertEqual(r["extensions"]["doorDecoration"]["minimumRemainingThickness"],26)
        with self.assertRaisesRegex(ValueError,"板厚"):
            self.build(cutDepth=18,machiningSide="both")
        with self.assertRaisesRegex(ValueError,"线条过密"):
            self.build(lineTool="v",cutDepth=16,vAngle=120,lineCount=80)
    def test_mother_widths_and_glass_procurement(self):
        r=self.build(doorType="mother",motherSide="right",motherRatio=.65)
        e=r["extensions"]["doorDecoration"]
        self.assertAlmostEqual(sum(e["leafWidths"])+e["gap"],self.defaults["width"])
        self.assertLess(e["leafWidths"][0],e["leafWidths"][1])
        display=self.build(pattern="glass_lattice")
        export=self.build("manufacturing",pattern="glass_lattice")
        self.assertEqual(len(display["items"]),4);self.assertEqual(len(export["items"]),2)
        for item in display["items"]:
            self.assertEqual(item["properties"]["manufacturing.partKind"],
                             "glass" if item["key"].endswith(".glass") else "plate")
    def test_hardware_and_trim_boundaries(self):
        r=self.build(pattern="panels",composition="continuous",border=20,meetingBorder=10)
        self.assertGreater(r["extensions"]["doorDecoration"]["skippedFeatures"],0)
        with self.assertRaisesRegex(ValueError,"五金保留区"):
            self.build(height=500,lockHeight=250,lockLength=100,hingeLength=140)
        with self.assertRaisesRegex(ValueError,"扣线宽度"):
            self.build(pattern="panels",trimWidth=30,webWidth=40)
    def test_mirror_and_continuity(self):
        r=self.build(lineAngle=30,composition="mirror")
        f=r["extensions"]["doorDecoration"]["features"]
        directions=[]
        for leaf in ("leaf.1","leaf.2"):
            m=next(v["primitive"] for v in f if v["leafId"]==leaf)
            directions.append(m["b"][0]-m["a"][0])
        self.assertLess(directions[0]*directions[1],0)
        r=self.build(lineAngle=0,composition="continuous",protectHardware=False)
        f=r["extensions"]["doorDecoration"]["features"]
        first=[v["primitive"] for v in f if v["leafId"]=="leaf.1"]
        second=[v["primitive"] for v in f if v["leafId"]=="leaf.2"]
        self.assertEqual(first,second)

if __name__=="__main__":unittest.main()
