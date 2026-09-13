"""Read the mounting flange, three outer fillets, and enclosed bore."""
import math
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=2:return False
    for loops,pose in g.frames(section):
        corners=q.polygon_corners(loops[0],t);bore=q.box(loops[1],g,t)
        if not corners or len(corners)!=6 or not bore:continue
        for mirror in (False,True):
            cs=[([-p[0],p[1]],r) for p,r in reversed(corners)] if mirror else corners
            for k in range(6):
                c=cs[k:]+cs[:k];p=[v[0] for v in c];r=[v[1] for v in c]
                if any(abs(p[a][1]-p[b][1])>t for a,b in ((0,1),(2,3),(4,5))):continue
                if any(abs(p[a][0]-p[b][0])>t for a,b in ((1,2),(3,4),(5,0))):continue
                w=p[3][0]-p[0][0];h=p[4][1]-p[3][1]
                f=p[2][1]-p[1][1];s=p[1][0]-p[0][0]
                wall=(w-bore['width'])/2
                if min(w,h,f,s,wall)<=t or s>wall+t:continue
                if abs(h-bore['depth']-2*wall)>t:continue
                cx=(p[0][0]+p[3][0])/2;cy=(p[3][1]+p[4][1])/2
                if math.dist(bore['center'],[-cx if mirror else cx,cy])>t:continue
                if any(r[i]>t for i in (0,1,2)) or abs(r[3]-r[4])>t or abs(r[3]-r[5])>t:continue
                return q.result(dict(width=w,depth=h,wallThickness=wall,flangeLength=f,
                    flangeThickness=min(s,wall),cornerRadius=r[3],innerRadius=bore['radius'],
                    mirrorX=mirror,geometrySource='idealizedFallback'),
                    q.shifted_pose(pose,[-cx if mirror else cx,p[3][1]]),'unique')
    return False
