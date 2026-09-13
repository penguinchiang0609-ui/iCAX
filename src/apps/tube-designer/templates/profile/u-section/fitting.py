"""Direct inverse of the equal-thickness trapezoidal U rib."""
import math
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        corners=q.polygon_corners(loops[0],t)
        if not corners or len(corners)!=8:continue
        for k in range(8):
            cs=corners[k:]+corners[:k];p=[c[0] for c in cs];rs=[c[1] for c in cs]
            x=[c[0] for c in p];y=[c[1] for c in p]
            if any(abs(y[a]-y[b])>t for a,b in ((0,3),(0,4),(0,7),(1,2),(5,6))):continue
            if any(abs(x[a]+x[b])>t for a,b in ((0,3),(1,2),(4,7),(5,6))):continue
            w=x[3]-x[0];b=x[2]-x[1];h=y[0]-y[1];wall=y[5]-y[1]
            if min(w,b,h,wall,h-wall,x[5])<=t or w<b-t:continue
            slope=(w-b)/(2*h);inset=wall*math.hypot(1,slope)
            if abs(x[4]-(w/2-inset))>t or abs(x[5]-(b/2+slope*wall-inset))>t:continue
            if any(rs[i]>t for i in (0,3,4,7)):continue
            r=rs[5]
            if abs(rs[6]-r)>t or abs(rs[1]-r-wall)>t or abs(rs[2]-r-wall)>t:continue
            return q.result(dict(width=w,bottomWidth=b,depth=h,wallThickness=wall,bendRadius=r),
                q.shifted_pose(pose,[0,y[1]]),'unique')
    return False
