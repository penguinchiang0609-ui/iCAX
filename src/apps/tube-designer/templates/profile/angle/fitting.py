"""Direct leg dimensions for cold-bent, rolled, sharp and welded angles."""
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        for raw,wall,r in q.strip_axes(loops[0],t):
            for p in (raw,list(reversed(raw))):
                if len(p)!=3:continue
                if abs(p[0][0]-p[1][0])>t or abs(p[1][1]-p[2][1])>t:continue
                w=p[2][0]-p[1][0];h=p[0][1]-p[1][1]
                if min(w,h)<=t:continue
                return q.result(dict(manufacturingRoute='ColdBent',width=w,depth=h,
                    wallThickness=wall,model0BendRadius=r,geometrySource='idealizedFallback'),
                    q.shifted_pose(pose,[(p[1][0]+p[2][0])/2,(p[0][1]+p[1][1])/2]))
        corners=q.polygon_corners(loops[0],t,chamfers=True)
        if not corners or len(corners)!=6:continue
        for k in range(6):
            cs=corners[k:]+corners[:k];x=[c[0][0] for c in cs];y=[c[0][1] for c in cs];rs=[c[1] for c in cs]
            if any(abs(x[a]-x[b])>t for a,b in ((1,2),(3,4),(5,0))):continue
            if any(abs(y[a]-y[b])>t for a,b in ((0,1),(2,3),(4,5))):continue
            w=x[1]-x[0];h=y[5]-y[0];wall=x[3]-x[0];f=y[2]-y[1]
            if min(w-wall,h-f,wall,f)<=t:continue
            if any(abs(rs[i]-rs[1])>t for i in (2,4,5)):continue
            params=dict(width=w,depth=h,wallThickness=wall,geometrySource='idealizedFallback')
            if rs[3]<0:
                if abs(rs[0])>t or abs(rs[1])>t:continue
                params.update(manufacturingRoute='Welded',model3FlangeThickness=f,model3WeldLeg=-rs[3])
            elif max(rs)<=t:
                params.update(manufacturingRoute='Precision',model2FlangeThickness=f)
            else:
                params.update(manufacturingRoute='HotRolled',model1FlangeThickness=f,
                    model1RootRadius=rs[3],model1HeelRadius=rs[0],model1ToeRadius=rs[1])
            return q.result(params,q.shifted_pose(pose,[(x[0]+x[1])/2,(y[0]+y[5])/2]))
    return False
