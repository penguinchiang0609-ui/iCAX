from copy import deepcopy
import unittest
from WindowCatalogueTests import package

class SteelStaircaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.d,cls.defaults,cls.m=package("straight_steel_staircase")
    def build(self,purpose="display",**changes):
        p=dict(self.defaults,**changes);before=deepcopy(p)
        r=self.m.generate(p,{"template":self.d,"geometryPurpose":purpose})
        self.assertEqual(p,before);self.assertEqual(r["parameters"],before)
        self.assertTrue(r["items"]);return r
    def test_route_structure_and_tread_matrix(self):
        for route in ("straight","straight_landing","l_turn","u_turn"):
            for system in ("mono","twin","side"):
                for support in ("cross_tube","tube_frame"):
                    for side in ("left","right"):
                        with self.subTest(route=route,system=system,support=support,side=side):
                            r=self.build(stairRoute=route,stringerSystem=system,treadSupport=support,turnDirection=side,
                                         totalRiserCount=6,floorHeight=1080,firstFlightRiserCount=3,railingSide="none")
                            info=r["extensions"]["steelStaircase"]
                            self.assertEqual(len(info["flights"]),1 if route=="straight" else 2)
                            self.assertEqual(sum(f["risers"] for f in info["flights"]),6)
                            self.assertEqual(len(info["stringerOffsets"]),1 if system=="mono" else 2)
    def test_plate_holes_and_real_cuts(self):
        r=self.build(railingSide="none")
        ops=[g for g in r["geometry"] if g["operator"]=="boolean"]
        self.assertTrue(any(g["arguments"]["operation"]=="intersect" for g in ops))
        self.assertTrue(any(g["arguments"]["operation"]=="subtract" for g in ops))
        bases=[i for i in r["items"] if i["key"].endswith(".base")]
        self.assertEqual(len(bases),2)
        for b in bases:self.assertEqual(len(b["properties"]["manufacturing.operations"]),4)
    def test_procurement_and_groups(self):
        for kind in ("wood","glass","stone"):
            d=self.build(treadType=kind,railingSide="none")
            m=self.build("manufacturing",treadType=kind,railingSide="none")
            self.assertGreater(len(d["items"]),len(m["items"]))
            self.assertFalse(any(i["properties"]["manufacturing.sourcing"]=="purchased" for i in m["items"]))
        self.assertTrue(any(g["quantity"]>1 for g in d["extensions"]["steelStaircase"]["partGroups"]))
    def test_inactive_fields(self):
        p=dict(railingSide="none",stairRoute="straight",connectionType="weld",treadType="none")
        a=self.build(**p)
        b=self.build(**p,firstFlightRiserCount=-1,landingLength=-1,wellGap=-1,
                     boltHoleDiameter=-1,connectionPlateThickness=-1,landingProfileType="inactive",
                     treadThickness=-1,treadGap=-1,railingHeight=-1,handrailProfileType="inactive",railingInfill="inactive",
                     maximumPostSpacing=-1,postEveryRisers=-1)
        self.assertEqual(a["geometry"],b["geometry"])
    def test_new_structures_and_nodes(self):
        for route in ('straight','straight_landing','l_turn','u_turn'):
            for kind in ('rect','channel','round','oval'):
                for bracket in ('tube','plate'):
                    with self.subTest(route=route,profile=kind,bracket=bracket):
                        r=self.build(stairRoute=route,stringerProfileType=kind,bracketType=bracket,
                                     floorHeight=1080,totalRiserCount=6,firstFlightRiserCount=3)
                        supports=[i for i in r['items'] if '.support.' in i['key']]
                        self.assertTrue(supports)
                        self.assertTrue(all(i['properties']['manufacturing.partKind']==('plate' if bracket=='plate' else 'tube') for i in supports))
            for purpose in ('display','manufacturing'):
                r=self.build(purpose,stairRoute=route,stringerConstruction='zigzag',floorHeight=1080,totalRiserCount=6,firstFlightRiserCount=3,
                             stringerProfileType='inactive',bracketType='plate',bracketThickness=-1)
                self.assertFalse(any('.support.' in i['key'] for i in r['items']))
                self.assertTrue(any(i['displayName']=='锯齿板梁' for i in r['items']))
    def test_continuous_guard_profiles(self):
        for kind in ('rect','round','oval','racetrack'):
            for route in ('straight_landing','l_turn','u_turn'):
                r=self.build(stairRoute=route,handrailProfileType=kind,postProfileType=kind,infillProfileType=kind,
                             floorHeight=1080,totalRiserCount=6,firstFlightRiserCount=3)
                self.assertTrue(any(i['key'].startswith('transition.') for i in r['items']))
                self.assertTrue(any(g['operator']=='boolean' and g['arguments']['operation']=='subtract' for g in r['geometry']))
    def test_channel_constructions(self):
        for kind in ('channel-cold-u','channel-hot-parallel','channel-hot-tapered','channel-welded'):
            r=self.build(stringerProfileType='channel',stringerChannelModel=kind,bracketType='plate',railingSide='none')
            self.assertTrue(r['items'])
    def test_narrow_well_continuous_return(self):
        r=self.build(stairRoute='u_turn',wellGap=100,postProfileType='oval',postWidth=100,postDepth=20,
                     floorHeight=1080,totalRiserCount=6,firstFlightRiserCount=3)
        self.assertTrue(any(i['key'].startswith('transition.') for i in r['items']))
        nodes={g['key']:g for g in r['geometry']}
        post='landing.1.guard.2.post.0'
        cut=nodes[post+'.transition_cope']['arguments']
        self.assertEqual(cut['tools'],[post+'.joint_envelope'])
        self.assertIn('keepConnectedTo',cut)
        joint=nodes[post+'.joint_envelope']['arguments']
        self.assertEqual(joint['operation'],'union')
        self.assertIn('transition.2.1.envelope.cut.1',joint['tools'])
        with self.assertRaisesRegex(ValueError,'井道间距'):
            self.build(stairRoute='u_turn',wellGap=0)
    def test_clear_failure_for_conflicting_nodes(self):
        with self.assertRaisesRegex(ValueError,"端板"):
            self.build(stairRoute="l_turn",landingLength=self.defaults["stairWidth"])
        with self.assertRaisesRegex(ValueError,"首级支架"):
            self.build(treadThickness=100)
        with self.assertRaisesRegex(ValueError,"支承区"):
            self.build(stringerWidth=25)
        with self.assertRaisesRegex(ValueError,"双梁间距"):
            self.build(stringerSystem="twin",stringerSpacing=100)
if __name__=="__main__":unittest.main()
