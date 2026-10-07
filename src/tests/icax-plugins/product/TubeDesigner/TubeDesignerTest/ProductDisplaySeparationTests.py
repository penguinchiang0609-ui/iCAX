"""Current public product display contracts and decorative boundary geometry."""
from copy import deepcopy
import math
import unittest
from unittest.mock import patch
from WindowCatalogueTests import package
from icax_template_sdk import NeutralModel


class ProductDisplaySeparationTests(unittest.TestCase):
    def build(self,name,changes=None,purpose="display"):
        descriptor,defaults,module=package(name)
        parameters={**defaults,**(changes or {})};before=deepcopy(parameters)
        original=NeutralModel.geometry
        def geometry(model,key,operator,**kwargs):
            if purpose=="display":self.assertNotEqual(operator,"boolean",key)
            return original(model,key,operator,**kwargs)
        with patch.object(NeutralModel,"geometry",geometry):
            result=module.generate(parameters,{"template":descriptor,"geometryPurpose":purpose})
        self.assertEqual(parameters,before);self.assertEqual(result["parameters"],before)
        self.assertTrue(result["items"])
        return result

    def test_display_never_constructs_manufacturing_boolean(self):
        for name in ("modular_guardrail","modular_guardrail_cross-straight",
                     "modular_guardrail_diamond-straight","modular_guardrail_glass-straight",
                     "straight_steel_staircase"):
            with self.subTest(name=name):self.build(name)
        for changes in ({"installation":"base_plate"},{"installation":"side_plate","pathMode":"continuous"}):
            self.build("modular_guardrail_glass-straight",changes)

    def test_hidden_glass_infill_drafts_neither_block_nor_change_geometry(self):
        for purpose in ("display","manufacturing"):
            first=self.build("modular_guardrail_glass-straight",purpose=purpose)
            second=self.build("modular_guardrail_glass-straight",{
                "infillWidth":999,"infillDepth":999,"infillWallThickness":999,
                "infillProfileId":"invalid-retained-draft","maximumVerticalClearGap":-1,
                "fixedBarCount":-1,"barDistribution":"invalid-retained-draft"},purpose)
            self.assertEqual(first["geometry"],second["geometry"])

    @unittest.skip("Deferred louver reference; no active product generation")
    def test_repeated_louver_blades_share_display_and_finished_prototypes(self):
        for purpose in ("display","manufacturing"):
            result=self.build("louver_window",purpose=purpose)
            graph={node["key"]:node for node in result["geometry"]}
            roots=[]
            for item in result["items"]:
                if not item["key"].startswith("blade."):continue
                key=item["representations"]["result"]
                self.assertEqual(graph[key]["operator"],"transform")
                roots.append(graph[key]["inputs"][0])
            self.assertEqual(len(roots),16);self.assertEqual(len(set(roots)),1)

    @unittest.skip("Deferred decorative-door reference; no active product generation")
    def test_door_patterns_keep_visible_boundaries_and_repeat_prototypes(self):
        for pattern in ("lines","diamond","octagon","round_scene","panels","glass_lattice"):
            for side in ("front","back","both"):
                result=self.build("decorative_door",{"pattern":pattern,"machiningSide":side,
                    "composition":"repeat","protectHardware":False,"columns":2,"rows":3})
                graph={node["key"]:node for node in result["geometry"]}
                leaves=[item for item in result["items"] if item["key"] in ("leaf.1","leaf.2")]
                roots=[graph[item["representations"]["result"]]["inputs"][0] for item in leaves]
                self.assertEqual(roots[0],roots[1])
                self.assertGreater(len(result["extensions"]["doorDecoration"]["features"]),0)
        result=self.build("decorative_door",{"pattern":"diamond","doorType":"single",
                           "protectHardware":False,"columns":2,"rows":3})
        self.assertEqual(len(result["extensions"]["doorDecoration"]["features"]),6)


@unittest.skip("Deferred decorative-door reference; not an active product")
class DecorativeDisplayBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _,_,module=package("decorative_door")
        cls.geometry=module.shared("door_display_geometry.py")

    def volume(self,cells,thickness):
        total=0
        for polygon,front,back in cells:
            for b,c in zip(polygon[1:],polygon[2:]):
                a=polygon[0]
                area=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2
                total+=area*sum(thickness-self.geometry.value(front,p)-self.geometry.value(back,p) for p in (a,b,c))/3
        return total

    def test_recess_through_and_protected_regions_match_exact_removed_volume(self):
        feature={"primitive":{"kind":"polygon","points":[[10,20],[90,20],[90,180],[10,180]]},
                 "side":"front","boundary":[0,100,0,200],"protected":[]}
        for depth in (4,40):
            cells=self.geometry.cells(100,200,40,[feature],0,depth,False,0)
            self.assertAlmostEqual(self.volume(cells,40),100*200*40-80*160*depth,places=5)
        feature["protected"]=[[40,60,50,150]]
        cells=self.geometry.cells(100,200,40,[feature],0,4,False,0)
        self.assertAlmostEqual(self.volume(cells,40),100*200*40-(80*160-20*100)*4,places=5)

    def test_v_groove_keeps_sloped_walls_depth_and_thickness(self):
        feature={"primitive":{"kind":"line","a":[10,100],"b":[90,100]},
                 "side":"front","boundary":[0,100,0,200],"protected":[]}
        cells=self.geometry.cells(100,200,40,[feature],8,4,True,90)
        self.assertAlmostEqual(self.volume(cells,40),100*200*40-80*8*4/2,places=5)
        self.assertTrue(any(abs(front[0])+abs(front[1])>0 for _,front,_ in cells))
        self.assertAlmostEqual(max(self.geometry.value(front,p) for poly,front,_ in cells for p in poly),4)

    def test_overlapping_pockets_are_union_not_double_subtraction(self):
        def feature(box):return {"primitive":{"kind":"polygon","points":self.geometry.rectangle(box)},
                                "side":"front","boundary":[0,100,0,200],"protected":[]}
        cells=self.geometry.cells(100,200,40,[feature((10,60,20,100)),feature((40,90,60,140))],0,5,False,0)
        self.assertAlmostEqual(self.volume(cells,40),800000-(4000+4000-20*40)*5,places=5)

    def test_round_boundary_display_chord_error_is_bounded(self):
        feature={"primitive":{"kind":"ring","center":[50,100],"radius":30},
                 "side":"front","boundary":[0,100,0,200],"protected":[]}
        regions=self.geometry.feature_regions(feature,4,3,False,0)
        self.assertLessEqual(32*(1-math.cos(math.pi/len(regions))),self.geometry.CHORD_ERROR)


if __name__=="__main__":unittest.main()
