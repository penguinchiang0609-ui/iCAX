"""Measure the complete flange/web boundary directly."""
import math
IMPLEMENTED = True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        corners=q.polygon_corners(loops[0],t,chamfers=True)
        if not corners or len(corners)!=12:continue
        for shift in range(12):
            c=corners[shift:]+corners[:shift]
            x=[a[0][0] for a in c];y=[a[0][1] for a in c];rs=[a[1] for a in c]
            if any(abs(y[a]-y[b])>t for a,b in ((0,1),(6,7),(2,11),(3,10),(4,9),(5,8))):continue
            if any(abs(x[a]-x[b])>t for a,b in ((1,2),(3,4),(5,6),(7,8),(9,10),(11,0))):continue
            if abs(x[0]+x[1])>t or abs(x[6]+x[7])>t or abs(y[0]+y[6])>t:continue
            w,wb,h=x[6]-x[7],x[1]-x[0],y[6]-y[0]
            web=x[3]-x[9];offset=(x[3]+x[9])/2
            lower=y[2]-y[0];upper=y[6]-y[5]
            if min(w-web-2*abs(offset),wb-web-2*abs(offset),web,lower,upper,y[4]-y[3])<=t:continue
            if any(abs(rs[i])>t for i in (0,1,6,7)):continue
            root,toe=rs[3],rs[2]
            if any(abs(rs[i]-root)>t for i in (4,9,10)) or any(abs(rs[i]-toe)>t for i in (5,8,11)):continue
            if root>t or abs(toe)>t:continue
            if any(abs(y[a]-y[b])>t for a,b in ((2,3),(4,5))):continue
            return q.result(dict(width=w,lowerWidth=wb,depth=h,wallThickness=web,
                flangeThickness=upper,lowerThickness=lower,webOffset=offset,weldLeg=-root),pose)
    return False
