import copy
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
        self.assertEqual(21,len(packages))
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
        groups={
            ('管',):['rect','round','racetrack','oval','curve-hollow','p-tube','polygon'],
            ('型材',):['angle','channel','t-section','i-section','bulb-flat','omega','round-bar','polygon-bar','rect-bar','z-section','sigma','open-tube','u-section'],
            ('多腔体',):['multi-cell'],
        }
        for path,ids in groups.items():
            for name in ids:
                d=runtime._system_package(CATALOG/name,preview=False)['descriptor']
                self.assertEqual(path[0],d['category'])
                self.assertEqual(list(path),d['catalog']['categoryPath'])
                self.assertTrue(evaluate(name)['contours'])

    def test_p_wing_is_independent(self):
        self.assertEqual(2,evaluate('p-tube')['contourCount'])
        self.assertEqual(85,evaluate('p-tube',flangeLength=45)['depth'])
        self.assertNotEqual(evaluate('p-tube')['contours'],evaluate('p-tube',mirrorX=True)['contours'])

    def test_bulb_is_a_fillet_and_slope_profile_not_a_semicircle(self):
        p=evaluate('bulb-flat')
        self.assertAlmostEqual(23,p['width'],places=7)
        self.assertAlmostEqual(120,p['depth'],places=7)
        self.assertEqual(1,p['contourCount'])
        self.assertEqual(5,sum(e['kind']=='arc' for e in p['contours'][0]['segments']))
        with self.assertRaises(ValueError):evaluate('bulb-flat',endRadius=4)

    def test_rectangle_mixed_corners_and_straight_hat(self):
        p=evaluate('rect',useOuterRadii=True,outerRadius1=1,outerRadius2=2,
                   outerRadius3=3,outerRadius4=4,useInnerRadii=True,
                   innerRadius1=0.5,innerRadius2=1,innerRadius3=1.5,innerRadius4=2)
        self.assertEqual(4,sum(e['kind']=='arc' for e in p['contours'][0]['segments']))
        self.assertNotEqual(p['contours'][0],p['contours'][1])
        with self.assertRaises(ValueError):evaluate('rect',innerOffsetX=2)
        self.assertTrue(evaluate('omega',width=80,flangeWidth=20)['contours'])

    def test_hat_hot_rolled_uses_partitioned_thickness_and_four_radii(self):
        package=runtime._system_package(CATALOG/'omega',preview=False)
        keys={item['key'] for item in package['descriptor']['parameters']}
        self.assertTrue({'useHotRolled','hotHeight','innerWidth','hotFlangeWidth',
                         'topThickness','legThickness','flangeThickness',
                         'rootRadius1','rootRadius2','freeEndRadiusUpper',
                         'freeEndRadiusLower'} <= keys)
        hot=evaluate('omega',useHotRolled=True,hotHeight=120,innerWidth=80,
                     hotFlangeWidth=50,topThickness=8,legThickness=6,
                     flangeThickness=8,rootRadius1=4,rootRadius2=5,
                     freeEndRadiusUpper=2,freeEndRadiusLower=1)
        self.assertEqual('HotRolled',hot['manufacturingRoute'])
        self.assertAlmostEqual(120,hot['depth'])
        self.assertEqual(12,sum(edge['kind']=='arc' for edge in hot['contours'][0]['segments']))
        cold=evaluate('omega',useHotRolled=False)
        self.assertEqual('ColdBent',cold['manufacturingRoute'])
        self.assertNotEqual(cold['contours'],hot['contours'])

    def test_channel_has_two_independent_inner_radii(self):
        package=runtime._system_package(CATALOG/'channel',preview=False)
        keys=[p['key'] for p in package['descriptor']['parameters']]
        self.assertIn('innerRadius1',keys)
        self.assertIn('innerRadius2',keys)
        self.assertNotIn('innerRadius',keys)
        lower=evaluate('channel',useInnerRadius=True,innerRadius1=1,innerRadius2=3)
        upper=evaluate('channel',useInnerRadius=True,innerRadius1=3,innerRadius2=1)
        self.assertEqual(1,lower['parameters']['innerRadius1'])
        self.assertEqual(3,lower['parameters']['innerRadius2'])
        self.assertNotEqual(lower['contours'],upper['contours'])

    def test_angle_and_channel_free_end_radii_are_advanced_and_effective(self):
        for name, values in (('angle',dict(useHotRolled=True,freeEndRadius=1)),
                             ('channel',dict(useHotRolled=True,freeEndRadius=1))):
            package=runtime._system_package(CATALOG/name,preview=False)
            keys={item['key'] for item in package['descriptor']['parameters']}
            self.assertTrue({'freeEndRadius','useIndependentFreeEndRadii',
                             'freeEndRadius1','freeEndRadius2'} <= keys)
            common=evaluate(name,**values)
            self.assertEqual(4 if name == 'angle' else 6,
                             sum(edge['kind']=='arc' for edge in common['contours'][0]['segments']))
            split=evaluate(name,useHotRolled=True,useIndependentFreeEndRadii=True,
                           freeEndRadius1=0.5,freeEndRadius2=0.75)
            self.assertNotEqual(common['contours'],split['contours'])
            for item in package['descriptor']['parameters']:
                if item['key'] in {'freeEndRadius','useIndependentFreeEndRadii',
                                   'freeEndRadius1','freeEndRadius2'}:
                    self.assertTrue(item.get('presentation',{}).get('advanced'))

    def test_hot_rolled_switch_extends_existing_angle_and_channel_profiles(self):
        for name in ('angle', 'channel'):
            package=runtime._system_package(CATALOG/name,preview=False)
            hot=next(item for item in package['descriptor']['parameters'] if item['key']=='useHotRolled')
            self.assertTrue(hot.get('presentation',{}).get('advanced'))
        generic_angle=evaluate('angle',useHotRolled=False,freeEndRadius=4)
        hot_angle=evaluate('angle',useHotRolled=True,freeEndRadius=4)
        self.assertEqual('Parametric',generic_angle['manufacturingRoute'])
        self.assertEqual('HotRolledAngle',hot_angle['manufacturingRoute'])
        self.assertNotEqual(generic_angle['contours'],hot_angle['contours'])
        generic_channel=evaluate('channel',useHotRolled=False,freeEndRadius=4)
        hot_channel=evaluate('channel',useHotRolled=True,flangeThickness=8.5,freeEndRadius=2)
        self.assertEqual('Parametric',generic_channel['manufacturingRoute'])
        self.assertEqual('HotRolledChannel',hot_channel['manufacturingRoute'])
        self.assertNotEqual(generic_channel['contours'],hot_channel['contours'])
        self.assertEqual(6,sum(edge['kind']=='arc' for edge in hot_channel['contours'][0]['segments']))

    def test_angle_inner_offset_moves_only_the_inner_boundary(self):
        centered=evaluate('angle',width=50,depth=80,wallThickness=6,outerRadius=9,
                          innerOffsetX=0,innerOffsetY=0)
        offset=evaluate('angle',width=50,depth=80,wallThickness=6,outerRadius=9,
                        innerOffsetX=1,innerOffsetY=1)
        base=centered['contours'][0]['segments']
        moved=offset['contours'][0]['segments']
        # The outer bend and both outer straight legs stay at the original
        # coordinates; only the inside side and its two end caps move.
        for index in (5,6,7):
            self.assertEqual(base[index],moved[index])
        self.assertNotEqual(base[0:5],moved[0:5])

    def test_angle_mirror_is_advanced_and_reflects_the_complete_section(self):
        package=runtime._system_package(CATALOG/'angle',preview=False)
        mirror=next(item for item in package['descriptor']['parameters'] if item['key']=='mirrorX')
        self.assertFalse(mirror['defaultValue'])
        self.assertTrue(mirror.get('presentation',{}).get('advanced'))
        display=json.loads((CATALOG/'angle'/'display.json').read_text(encoding='utf-8'))
        self.assertEqual('mirrorX',display['views']['section']['parameterDiagram']['mirrorParameter'])
        self.assertEqual('左右镜像',display['views']['right']['fields']['mirrorX']['title']['zh-CN'])
        values=dict(width=50,depth=80,wallThickness=6,outerRadius=9,
                    innerOffsetX=1,innerOffsetY=-2,useHotRolled=True,
                    useIndependentFreeEndRadii=True,freeEndRadius1=1,freeEndRadius2=2)
        normal=evaluate('angle',**values)
        mirrored=evaluate('angle',**values,mirrorX=True)
        expected=copy.deepcopy(normal['contours'])
        for contour in expected:
            for edge in contour['segments']:
                for key in ('start','middle','end','center'):
                    if key in edge:edge[key]=[-edge[key][0],edge[key][1]]
        self.assertEqual(expected,mirrored['contours'])
        self.assertTrue(mirrored['parameters']['mirrorX'])

    def test_angle_free_end_radius_equal_to_wall_is_valid(self):
        p=evaluate('angle',width=50,depth=80,wallThickness=6,outerRadius=9,
                   useHotRolled=True,freeEndRadius=6)
        self.assertEqual(4,sum(edge['kind']=='arc' for edge in p['contours'][0]['segments']))

    def test_zero_outer_radius_defaults_to_a_sharp_inner_corner(self):
        angle=evaluate('angle',width=50,depth=80,wallThickness=6,outerRadius=0)
        channel=evaluate('channel',width=50,depth=80,wallThickness=6,outerRadius=0)
        self.assertTrue(angle['contours'])
        self.assertTrue(channel['contours'])
        self.assertEqual(0,angle['parameters']['outerRadius'])
        self.assertEqual(0,channel['parameters']['outerRadius'])

    def test_hot_rolled_switch_extends_existing_t_and_i_profiles(self):
        for name in ('t-section', 'i-section'):
            package=runtime._system_package(CATALOG/name,preview=False)
            hot=next(item for item in package['descriptor']['parameters'] if item['key']=='useHotRolled')
            self.assertTrue(hot.get('presentation',{}).get('advanced'))
        generic=evaluate('i-section',useHotRolled=False)
        hot=evaluate('i-section',useHotRolled=True,legEndRadius=2)
        self.assertEqual('Parametric',generic['manufacturingRoute'])
        self.assertEqual('HotRolledI',hot['manufacturingRoute'])
        self.assertNotEqual(generic['contours'],hot['contours'])
        self.assertEqual(8,sum(edge['kind']=='arc' for edge in hot['contours'][0]['segments']))
        self.assertEqual('Parametric',evaluate('t-section',useHotRolled=False)['manufacturingRoute'])
        self.assertEqual('HotRolledCutT',evaluate('t-section',useHotRolled=True)['manufacturingRoute'])
        self.assertEqual(
            evaluate('t-section',useHotRolled=False)['contours'],
            evaluate('t-section',useHotRolled=True)['contours'])

    def test_bulb_flat_uses_only_nominal_parameters_and_supports_mirror(self):
        package=runtime._system_package(CATALOG/'bulb-flat',preview=False)
        keys={item['key'] for item in package['descriptor']['parameters']}
        self.assertEqual({'width','depth','wallThickness','bulbRadius1','bulbRadius2','endRadius','mirrorX'},keys)
        self.assertNotEqual(evaluate('bulb-flat')['contours'],evaluate('bulb-flat',mirrorX=True)['contours'])

    def test_flat_bar_hot_rolled_switch_controls_the_only_corner_radius(self):
        plain=evaluate('rect-bar',useHotRolled=False,cornerRadius=3)
        baseline=evaluate('rect-bar')
        self.assertEqual(baseline['contours'],plain['contours'])
        self.assertEqual(3,plain['parameters']['cornerRadius'])
        self.assertEqual(0,sum(edge['kind']=='arc' for edge in plain['contours'][0]['segments']))
        hot=evaluate('rect-bar',useHotRolled=True,cornerRadius=3)
        self.assertEqual('HotRolledFlatBar',hot['manufacturingRoute'])
        self.assertEqual(4,sum(edge['kind']=='arc' for edge in hot['contours'][0]['segments']))
        self.assertNotEqual(plain['contours'],hot['contours'])

    def test_polygon_bar_has_only_three_basic_parameters_and_real_fillets(self):
        package=runtime._system_package(CATALOG/'polygon-bar',preview=False)
        self.assertEqual({'radius','sideCount','cornerRadius'},
                         {item['key'] for item in package['descriptor']['parameters']})
        self.assertTrue(all(not item.get('presentation',{}).get('advanced')
                            for item in package['descriptor']['parameters']))
        display=json.loads((CATALOG/'polygon-bar'/'display.json').read_text(encoding='utf-8'))
        self.assertEqual({'dimensions'},set(display['views']['right']['groups']))
        rounded=evaluate('polygon-bar',radius=37,sideCount=7,cornerRadius=2.5)
        edges=rounded['contours'][0]['segments']
        self.assertEqual(7,sum(edge['kind']=='line' for edge in edges))
        self.assertEqual(7,sum(edge['kind']=='arc' for edge in edges))
        self.assertEqual({'radius':37,'sideCount':7,'cornerRadius':2.5},rounded['parameters'])
        sharp=evaluate('polygon-bar',radius=37,sideCount=7,cornerRadius=0)
        self.assertEqual(7,len(sharp['contours'][0]['segments']))
        self.assertTrue(all(edge['kind']=='line' for edge in sharp['contours'][0]['segments']))
        with self.assertRaises(ValueError):
            evaluate('polygon-bar',radius=10,sideCount=8,cornerRadius=10)

    def test_z_section_lips_share_defaults_and_allow_independent_overrides(self):
        plain=evaluate('z-section',useLips=False,lipLength=30,topLipLength=40)
        baseline=evaluate('z-section')
        self.assertEqual(baseline['contours'],plain['contours'])
        common=evaluate('z-section',useLips=True,lipLength=16,lipAngle=80)
        split=evaluate('z-section',useLips=True,useIndependentLips=True,
                       topLipLength=20,bottomLipLength=12,
                       topLipAngle=75,bottomLipAngle=105)
        self.assertEqual(8,sum(edge['kind']=='arc' for edge in common['contours'][0]['segments']))
        self.assertNotEqual(common['contours'],split['contours'])

    def test_sigma_is_one_equal_thickness_path_with_documented_advanced_modes(self):
        package=runtime._system_package(CATALOG/'sigma',preview=False)
        self.assertEqual({'display.json','fitting.py'},set(package['resources']))
        baseline=evaluate('sigma')
        self.assertEqual(1,baseline['contourCount'])
        self.assertEqual(16,sum(edge['kind']=='arc' for edge in baseline['contours'][0]['segments']))
        inactive=evaluate('sigma',useIndependentSides=False,topFlangeWidth=100,
                          useIndependentRadii=False,bendRadius1=0)
        self.assertEqual(baseline['contours'],inactive['contours'])
        asymmetric=evaluate('sigma',useIndependentSides=True,
                            topFlangeWidth=76,bottomFlangeWidth=64,
                            topLipLength=18,bottomLipLength=22,
                            topOuterWebHeight=32,bottomOuterWebHeight=38,
                            upperTransitionRise=18,lowerTransitionRise=22,
                            topLipAngle=80,bottomLipAngle=100)
        local_radii=evaluate('sigma',useIndependentRadii=True,
                             bendRadius1=1,bendRadius2=2,bendRadius3=3,bendRadius4=4,
                             bendRadius5=5,bendRadius6=4,bendRadius7=2,bendRadius8=1)
        self.assertNotEqual(baseline['contours'],asymmetric['contours'])
        self.assertNotEqual(baseline['contours'],local_radii['contours'])
        self.assertNotEqual(baseline['contours'],evaluate('sigma',mirrorX=True)['contours'])
        self.assertNotEqual(baseline['contours'],evaluate('sigma',mirrorY=True)['contours'])
        # The documented zero values are real geometric degenerations, not
        # alternate hard-coded models: e=0 becomes a C section and C=0
        # removes the two lips while keeping one continuous equal-thickness strip.
        for simplified in (evaluate('sigma',centerWebOffset=0),
                           evaluate('sigma',lipLength=0),
                           evaluate('sigma',centerWebOffset=0,lipLength=0)):
            self.assertEqual(1,simplified['contourCount'])
            self.assertTrue(simplified['contours'][0]['segments'])
        with self.assertRaises(ValueError):
            evaluate('sigma',depth=80,outerWebHeight=30,transitionRise=20)

if __name__=='__main__':unittest.main()
