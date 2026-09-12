import json
import math
import unittest
from ProfileFamilyTests import runtime,CATALOG

def evaluate(name,**values):
    p=runtime._system_package(CATALOG/name,preview=False)
    return runtime._evaluate(p['descriptor'],p['scriptSource'],values,p['packageDigest'],p['sourceFileName'],p['resources'])

class ProfileCompletenessTests(unittest.TestCase):
    def test_catalog_previews_and_dimension_values_are_consistent(self):
        packages=runtime._list_system_packages(CATALOG)
        self.assertEqual(24,len(packages))
        for package in packages:
            p=package['previewProfile']
            self.assertTrue(p['contours'])
            annotations=p['parameterDiagram']['annotations']
            self.assertTrue(annotations,package['name'])
            for a in annotations:
                if a['kind']=='linear':
                    axis=0 if a['axis']=='x' else 1
                    self.assertAlmostEqual(abs(a['from'][axis]-a['to'][axis]),
                        abs(p['parameters'][a['parameter']]),places=6,msg=package['name']+a['parameter'])

    def test_u_rib_has_sloping_equal_thickness_walls_and_concentric_bends(self):
        p=evaluate('u-section')
        self.assertAlmostEqual(300,p['width'])
        self.assertAlmostEqual(280,p['depth'])
        edges=p['contours'][0]['segments']
        arcs=[e for e in edges if e['kind']=='arc']
        self.assertEqual(4,len(arcs))
        def circle(e):
            a,m,b=e['start'],e['middle'],e['end']
            x,y=m[0]-a[0],m[1]-a[1];u,v=b[0]-a[0],b[1]-a[1]
            d=2*(x*v-y*u)
            c=[a[0]+(v*(x*x+y*y)-y*(u*u+v*v))/d,
               a[1]+(x*(u*u+v*v)-u*(x*x+y*y))/d]
            return c,math.dist(a,c)
        circles=[circle(e) for e in arcs]
        self.assertEqual([24,24,32,32],sorted(round(r) for _,r in circles))
        for c,r in circles:
            self.assertTrue(any(math.dist(c,d)<1e-7 and abs(abs(r-s)-8)<1e-7 for d,s in circles))
        slope=65/280
        walls=[e for e in edges if e['kind']=='line' and abs(e['end'][1]-e['start'][1])>100]
        self.assertEqual(4,len(walls))
        for e in walls:
            self.assertAlmostEqual(slope,abs((e['end'][0]-e['start'][0])/(e['end'][1]-e['start'][1])))
        self.assertTrue(evaluate('u-section',bottomWidth=300)['contours'])
        self.assertTrue(evaluate('u-section',bendRadius=0)['contours'])
        for values in ({'bottomWidth':301},{'wallThickness':150},{'bendRadius':500}):
            with self.assertRaises(ValueError):evaluate('u-section',**values)

    def test_every_requested_entry_exists_and_has_correct_category(self):
        groups={'常用管材':['rect','round','racetrack','oval'],
                '常用型材':['angle','channel','i-section','unequal-i','t-section'],
                '其他':['curve-hollow','p-tube','bulb-flat','rect-bar','u-section','omega']}
        for category,ids in groups.items():
            for name in ids:
                d=runtime._system_package(CATALOG/name,preview=False)['descriptor']
                self.assertEqual(category,d['category'])
                self.assertTrue(evaluate(name)['contours'])

    def test_unequal_flanges_and_p_wing_are_independent(self):
        a=evaluate('unequal-i');b=evaluate('unequal-i',lowerWidth=150,lowerThickness=12,webOffset=5)
        self.assertEqual(150,b['width']);self.assertNotEqual(a['contours'],b['contours'])
        self.assertEqual(2,evaluate('p-tube')['contourCount'])
        self.assertEqual(85,evaluate('p-tube',flangeLength=45)['depth'])
        self.assertNotEqual(evaluate('p-tube')['contours'],evaluate('p-tube',mirrorX=True)['contours'])

    def test_bulb_is_a_fillet_and_slope_profile_not_a_semicircle(self):
        p=evaluate('bulb-flat')
        self.assertAlmostEqual(23,p['width'],places=7)
        self.assertAlmostEqual(120,p['depth'],places=7)
        self.assertEqual(1,p['contourCount'])
        self.assertEqual(5,sum(e['kind']=='arc' for e in p['contours'][0]['segments']))
        with self.assertRaises(ValueError):evaluate('bulb-flat',edgeRadius=4)

    def test_rectangle_mixed_corners_and_straight_hat(self):
        specs=[{'mode':'sharp'},{'mode':'arc','radius':3},{'mode':'chamfer','incoming':3,'outgoing':3},{'mode':'custom','incoming':3,'outgoing':3}]
        p=evaluate('rect',outerCorners=json.dumps(specs))
        self.assertTrue(any(e['kind']=='bezier' for e in p['contours'][0]['segments']))
        with self.assertRaises(ValueError):evaluate('rect',outerCorners='[{}]')
        self.assertTrue(evaluate('omega',width=80,crownWidth=80)['contours'])

    def test_source_requires_boundary_and_spline_envelope_and_swap_are_supported(self):
        with self.assertRaises(ValueError):evaluate('bulb-flat',geometrySource='scan')
        loop={'kind':'path','closed':True,'segments':[
            {'kind':'line','start':[0,0],'end':[20,0]},
            {'kind':'bezier','controlPoints':[[20,0],[30,0],[30,10],[20,10]]},
            {'kind':'line','start':[20,10],'end':[0,10]},
            {'kind':'line','start':[0,10],'end':[0,0]}]}
        p=evaluate('bulb-flat',geometrySource='scan',sourceRevision='test-only boundary',materialBoundary=json.dumps([loop]))
        self.assertAlmostEqual(27.5,p['width'],places=5)
        self.assertEqual('scan',p['geometrySource'])
        package=runtime._system_package(CATALOG/'bulb-flat',preview=False)
        with runtime._script_context(package['scriptSource'],package['packageDigest'],package['resources'],package['descriptor']) as ns:
            swapped=ns['swap']([loop],True)
            self.assertEqual([0,20],swapped[0]['segments'][1]['controlPoints'][0])

if __name__=='__main__':unittest.main()
