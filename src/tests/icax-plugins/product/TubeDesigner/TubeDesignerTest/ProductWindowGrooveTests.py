"""Analytical checks of the product-owned cutter, without loading any mould."""
import math
import unittest
from SingleSecurityWindowGeometryTests import generate, graph


def contour(document):
    return graph(document)['outer_frame.continuous.0001.export.groove.0001.profile']['arguments']['contours'][0]


def arc_radius(edge):
    a,b,c=(edge[k] for k in ('start','middle','end'))
    twice_area=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
    return math.dist(a,b)*math.dist(b,c)*math.dist(c,a)/(2*twice_area)


class ProductWindowGrooveTests(unittest.TestCase):
    def build(self,**p):
        return generate(frameLayout='four_sides',**p)

    def test_radius_is_exact_and_male_preserves_root(self):
        for radius in (0,1,18,40):
            for male in (False,True):
                shape=contour(self.build(frameJoinType='v_groove_90:rounded_v',vGrooveRadius=radius,vGrooveMaleFemale=male))
                arcs=[e for e in shape['segments'] if e['kind']=='arc']
                self.assertEqual(len(arcs),2 if radius else 0)
                for edge in arcs:self.assertAlmostEqual(arc_radius(edge),radius,places=7)
        with self.assertRaisesRegex(ValueError,'圆角过大'):
            self.build(frameJoinType='v_groove_90:rounded_v',vGrooveRadius=1000)

    def test_k_changes_stock_and_root_width_not_radius(self):
        docs=[self.build(frameJoinType='v_groove_90:rounded_v',vGrooveKFactor=k) for k in (0,1)]
        def stock(d):return next(i['properties']['length'] for i in d['items'] if i['key']=='outer_frame.continuous.0001')
        self.assertAlmostEqual(stock(docs[1])-stock(docs[0]),4*math.pi/2*1.2,places=3)
        roots=[]
        for d in docs:
            edges=contour(d)['segments']
            root=min(e['start'][1] for e in edges)
            floor=next(e for e in edges if e['kind']=='line' and abs(e['start'][1]-root)<1e-8 and abs(e['end'][1]-root)<1e-8)
            roots.append(math.dist(floor['start'],floor['end']))
            for edge in edges:
                if edge['kind']=='arc':self.assertAlmostEqual(arc_radius(edge),18,places=7)
        self.assertAlmostEqual(roots[1]-roots[0],math.pi/2*1.2)

    def test_relief_keeps_floor_and_blind_cut_does_not_reach_far_wall(self):
        for blind in (False,True):
            doc=self.build(vGrooveReliefHole=True,vGrooveReliefNoThrough=blind)
            nodes=graph(doc)
            key='outer_frame.continuous.0001.export.groove.0001.relief'
            profile=nodes[key+'.profile']['arguments']
            origin=profile['placement']['origin'];r=profile['contours'][0]['radius']
            self.assertAlmostEqual(origin[2]-r,-38/2+2)
            end=origin[1]+nodes[key+'.solid']['arguments']['vector'][1]
            if blind:
                self.assertGreater(end,-25/2+1.2)
                self.assertLess(end,25/2-1.2)
            else:self.assertGreater(end,25/2)

    def test_side_arcs_are_exact_mirrors_and_keep_arc_with_male(self):
        shapes=[]
        for style in ('left_arc','right_arc'):
            for male in (False,True):
                shape=contour(self.build(frameJoinType='v_groove_90:'+style,vGrooveMaleFemale=male))
                arcs=[e for e in shape['segments'] if e['kind']=='arc']
                self.assertEqual(len(arcs),1)
                self.assertAlmostEqual(arc_radius(arcs[0]),36,places=7)
                if not male:shapes.append(shape)
        points=[[(round(e[k][0],6),round(e[k][1],6)) for e in s['segments'] for k in ('start','end')] for s in shapes]
        centre=(min(x for x,y in points[0])+max(x for x,y in points[1]))/2
        self.assertEqual({(round(2*centre-x,6),y) for x,y in points[0]},set(points[1]))

    def test_hidden_values_and_unsupported_section(self):
        self.build(vGrooveRadius=float('nan'),vGrooveReliefDiameter=float('nan'))
        with self.assertRaisesRegex(ValueError,'支承壁'):
            self.build(frameProfileType='round')
        for purpose in ('display','manufacturing'):
            with self.assertRaisesRegex(ValueError,'释放孔直径'):
                generate(purpose,frameLayout='four_sides',vGrooveReliefHole=True,vGrooveReliefDiameter=100)


if __name__=='__main__':unittest.main()
