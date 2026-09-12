import copy
import importlib.util
import math
from pathlib import Path
import unittest

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'src/apps/tube-designer/templates').is_dir())
spec=importlib.util.spec_from_file_location('catalog_axis_test',ROOT/'src/apps/tube-designer/templates/_shared/tube_profile_catalog.py')
catalog=importlib.util.module_from_spec(spec)
import sys
sys.modules[spec.name]=catalog
spec.loader.exec_module(catalog)

class AxisTransforms(unittest.TestCase):
    def test_rotated_ellipse_arc_reflection_and_round_trip(self):
        arc={'kind':'ellipseArc','center':[4,-7],'majorRadius':20,'minorRadius':8,
             'rotation':0.37,'startAngle':-0.8,'endAngle':4.9}
        contour={'kind':'path','segments':[arc]}
        original=copy.deepcopy(contour)
        reflected=catalog._swap_contour_axes(contour)['segments'][0]
        def point(s,t):
            a=s['majorRadius']*math.cos(t);b=s['minorRadius']*math.sin(t);r=s['rotation']
            return [s['center'][0]+a*math.cos(r)-b*math.sin(r),s['center'][1]+a*math.sin(r)+b*math.cos(r)]
        for i in range(21):
            t=arc['startAngle']+(arc['endAngle']-arc['startAngle'])*i/20
            self.assertLess(math.dist(point(arc,t)[::-1],point(reflected,-t)),1e-10)
        self.assertEqual(original,contour)
        twice=catalog._swap_contour_axes({'kind':'path','segments':[reflected]})['segments'][0]
        self.assertAlmostEqual(arc['rotation'],twice['rotation'])
        self.assertEqual(arc['center'],twice['center'])

if __name__=='__main__':unittest.main()
