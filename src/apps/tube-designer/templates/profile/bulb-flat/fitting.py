"""Read the five tangent intersections of the bulb-flat boundary."""
import math
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        corners=q.polygon_corners(loops[0],t)
        if not corners or len(corners)!=5:continue
        for mirror in (False,True):
            cs=[([-p[0],p[1]],r) for p,r in reversed(corners)] if mirror else corners
            for k in range(5):
                c=cs[k:]+cs[:k];p=[v[0] for v in c];r=[v[1] for v in c]
                if abs(p[0][1]-p[1][1])>t or abs(p[3][1]-p[4][1])>t:continue
                if abs(p[0][0]-p[4][0])>t or abs(p[1][0]-p[2][0])>t:continue
                width=p[4][1]-p[0][1];wall=p[1][0]-p[0][0]
                dx=p[3][0]-p[2][0];dy=p[3][1]-p[2][1]
                if min(width,wall,dx,dy,p[2][1]-p[1][1])<=t:continue
                if abs(dy-dx*math.tan(math.pi/6))>t:continue
                if abs(r[0]-r[1])>t or abs(r[0]-r[4])>t or abs(r[2]-r[3])>t:continue
                projection=dx-r[3]*(1/math.tan(math.pi/12)-1)
                if projection<=t or 2*r[0]>=wall-t:continue
                origin=[-p[0][0] if mirror else p[0][0],p[0][1]]
                return q.result(dict(width=width,wallThickness=wall,bulbProjection=projection,
                    bulbRadius=r[3],edgeRadius=r[0],mirrorX=mirror,geometrySource='idealizedFallback'),
                    q.shifted_pose(pose,origin),'unique')
    return False
