"""Measure T flange, offset web, and rolled or welded root corners."""
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        corners=q.polygon_corners(loops[0],t,chamfers=True)
        if not corners or len(corners)!=8:continue
        for shift in range(8):
            c=corners[shift:]+corners[:shift];x=[v[0][0] for v in c];y=[v[0][1] for v in c];r=[v[1] for v in c]
            if any(abs(y[a]-y[b])>t for a,b in ((0,1),(0,4),(0,5),(2,3),(6,7))):continue
            if any(abs(x[a]-x[b])>t for a,b in ((1,2),(3,4),(5,6),(7,0))):continue
            w=x[5]-x[0];h=y[6]-y[2];wall=x[3]-x[2];f=y[6]-y[5];offset=(x[2]+x[3]-x[0]-x[5])/2
            if min(w-wall-2*abs(offset),h-f,wall,f)<=t:continue
            if any(abs(r[i])>t for i in (2,3,6,7)) or abs(r[1]-r[4])>t or abs(r[0]-r[5])>t:continue
            origin=[(x[0]+x[5])/2,(y[2]+y[6])/2]
            index=1 if r[1]<0 else 0
            if r[0]<0 or (index and r[0]>t):continue
            params=dict(sectionModel='t-welded' if index else 't-split',width=w,depth=h,wallThickness=wall,geometrySource='idealizedFallback')
            params.update({f'model{index}FlangeThickness':f,f'model{index}WebOffset':offset})
            if index:params['model1WeldLeg']=-r[1]
            else:params.update(model0RootRadius=r[1],model0ToeRadius=r[0])
            return q.result(params,q.shifted_pose(pose,origin))
    return False
