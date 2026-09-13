"""Infer centreline dimensions from both measured strip boundaries."""
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        for raw,wall,r in q.strip_axes(loops[0],t):
            for p in (raw,list(reversed(raw))):
                x=[a[0] for a in p];y=[a[1] for a in p]
                if len(p)==4:
                    if abs(y[0]-y[1])>t or abs(x[1]-x[2])>t or abs(y[2]-y[3])>t:continue
                    w=x[1]-x[0];h=y[2]-y[1];upper=x[3]-x[2]
                    if min(w,h,upper)<=t:continue
                    params=dict(sectionModel='z-cold',width=w,depth=h,wallThickness=wall,model0BendRadius=r,model0UpperWidth=upper)
                    origin=[x[1],(y[1]+y[2])/2]
                elif len(p)==6:
                    if any(abs(x[a]-x[b])>t for a,b in ((0,1),(2,3),(4,5))):continue
                    if abs(y[1]-y[2])>t or abs(y[3]-y[4])>t:continue
                    w=x[2]-x[1];h=y[3]-y[2];lip=y[0]-y[1]
                    if min(w,h,lip)<=t or abs(x[4]-x[3]-w)>t or abs(y[4]-y[5]-lip)>t:continue
                    params=dict(sectionModel='z-cold-lipped',width=w,depth=h,wallThickness=wall,model1BendRadius=r,model1LipLength=lip)
                    origin=[x[2],(y[2]+y[3])/2]
                else:continue
                return q.result(params,q.shifted_pose(pose,origin))
    return False
