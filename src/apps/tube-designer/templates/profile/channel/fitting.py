"""Direct measurement of open-channel walls, lips, slope and roots."""
import math
IMPLEMENTED=True
def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        for raw,wall,r in q.strip_axes(loops[0],t):
            for p in (raw,list(reversed(raw))):
                x=[a[0] for a in p];y=[a[1] for a in p]
                params=dict(wallThickness=wall,geometrySource='idealizedFallback')
                if len(p)==4:
                    if abs(y[0]-y[1])>t or abs(x[1]-x[2])>t or abs(y[2]-y[3])>t:continue
                    w=x[0]-x[1];h=y[2]-y[1];upper=x[3]-x[2]
                    if min(w,h,upper)<=t:continue
                    if abs(w-upper)<=t:params.update(sectionModel='channel-cold-u',model0BendRadius=r)
                    else:params.update(sectionModel='channel-cold-unequal',model2BendRadius=r,model2UpperWidth=upper)
                    origin=[(x[0]+x[1])/2,(y[1]+y[2])/2]
                elif len(p)==6:
                    if any(abs(x[a]-x[b])>t for a,b in ((0,1),(2,3),(4,5),(1,4))):continue
                    if abs(y[1]-y[2])>t or abs(y[3]-y[4])>t:continue
                    w=x[1]-x[2];h=y[3]-y[2];lip=y[0]-y[1]
                    if min(w,h,abs(lip))<=t or abs(y[4]-y[5]-lip)>t:continue
                    index=3 if lip>0 else 4
                    params.update(sectionModel='channel-lipped-inward' if index==3 else 'channel-lipped-outward')
                    params.update({f'model{index}BendRadius':r,f'model{index}LipLength':abs(lip)})
                    origin=[(x[1]+x[2])/2,(y[2]+y[3])/2]
                else:continue
                params.update(width=w,depth=h)
                return q.result(params,q.shifted_pose(pose,origin))
        # Sloping flange lines remain support lines; only welded mode collapses diagonals.
        for weld in (False,True):
            corners=q.polygon_corners(loops[0],t,chamfers=weld)
            if not corners or len(corners)!=8:continue
            for k in range(8):
                cs=corners[k:]+corners[:k];x=[c[0][0] for c in cs];y=[c[0][1] for c in cs];rs=[c[1] for c in cs]
                if any(abs(x[a]-x[b])>t for a,b in ((1,2),(3,4),(5,6),(7,0),(1,6))):continue
                if abs(y[0]-y[1])>t or abs(y[6]-y[7])>t:continue
                w=x[1]-x[0];h=y[6]-y[1];wall=x[3]-x[0];lower=y[2]-y[1];upper=y[6]-y[5]
                if min(w-wall,wall,lower,upper,y[4]-y[3])<=t:continue
                if any(abs(rs[i])>t for i in (0,1,6,7)) or abs(rs[3]-rs[4])>t or abs(rs[2]-rs[5])>t:continue
                params=dict(width=w,depth=h,wallThickness=wall,geometrySource='idealizedFallback')
                rise=y[3]-y[2]
                if rs[3]<0:
                    if abs(rise)>t or abs(y[5]-y[4])>t or abs(rs[2])>t:continue
                    params.update(sectionModel='channel-welded',model7FlangeThickness=upper,
                        model7LowerThickness=lower,model7WeldLeg=-rs[3])
                else:
                    if abs(upper-lower)>t or rise < -t or abs(y[5]-y[4]-rise)>t or rs[2]<0:continue
                    index=6 if abs(rise)<=t else 5
                    params.update(sectionModel='channel-hot-parallel' if index==6 else 'channel-hot-tapered')
                    params.update({f'model{index}FlangeThickness':upper,f'model{index}RootRadius':rs[3],f'model{index}ToeRadius':rs[2]})
                    if index==5:params['model5FlangeSlope']=math.degrees(math.atan2(rise,w-wall))
                return q.result(params,q.shifted_pose(pose,[(x[0]+x[1])/2,(y[0]+y[6])/2]))
    return False
