"""Infer centreline dimensions from both measured strip boundaries."""
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        for raw,wall,r in q.strip_axes(loops[0],t):
            for p in (raw,list(reversed(raw))):
                x=[a[0] for a in p];y=[a[1] for a in p]
                if len(p)!=6:continue
                if any(abs(y[a]-y[b])>t for a,b in ((0,1),(0,4),(0,5),(2,3))):continue
                if any(abs(x[a]+x[b]-x[0]-x[5])>t for a,b in ((1,4),(2,3))):continue
                w=x[4]-x[1];h=y[2]-y[1];a=x[3]-x[2];f=x[1]-x[0]
                if min(w,h,a,f)<=t or a>w+t:continue
                params=dict(width=w,depth=h,crownWidth=a,flangeWidth=f,wallThickness=wall,bendRadius=r)
                origin=[(x[0]+x[5])/2,(y[0]+y[2])/2]
                return q.result(params,q.shifted_pose(pose,origin))
    return False
