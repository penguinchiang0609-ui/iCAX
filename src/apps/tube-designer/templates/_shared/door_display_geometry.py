"""Display-only planar boundary construction; no CSG or manufacturing cutters.

Convex cells carry affine front/back recess depths. Splitting in 2D preserves
through openings, recessed floors, V walls and protected hardware regions.
The result is a compound of planar display faces, never manufacturing input.
Circular display paths use a bounded 0.2 mm chord error; CAM retains exact arcs.
"""
import math

EPS = 1.e-7
CHORD_ERROR = .2


def clean(points):
    result=[]
    for point in points:
        if not result or math.dist(point,result[-1])>EPS:
            result.append(point)
    if len(result)>1 and math.dist(result[0],result[-1])<=EPS:result.pop()
    changed=True
    while changed and len(result)>3:
        changed=False
        for i,b in enumerate(result):
            a,c=result[i-1],result[(i+1)%len(result)]
            if abs((b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0]))<=EPS:
                result.pop(i);changed=True;break
    if len(result)<3:return []
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(result,result[1:]+result[:1]))
    if abs(area)<=EPS:return []
    return result if area>0 else result[::-1]


def clip(points,normal,limit):
    result=[]
    for a,b in zip(points,points[1:]+points[:1]):
        da=sum(a[i]*normal[i] for i in range(2))-limit
        db=sum(b[i]*normal[i] for i in range(2))-limit
        if da<=EPS:result.append(a)
        if (da < -EPS and db > EPS) or (da > EPS and db < -EPS):
            f=da/(da-db);result.append([a[i]+f*(b[i]-a[i]) for i in range(2)])
    return clean(result)


def rectangle(box):
    l,r,b,t=box
    return [[l,b],[r,b],[r,t],[l,t]]


def subtract_convex(points,tool):
    """Disjoint convex outside pieces and the intersection."""
    remaining=points;outside=[]
    for a,b in zip(tool,tool[1:]+tool[:1]):
        normal=(b[1]-a[1],a[0]-b[0]);limit=sum(normal[i]*a[i] for i in range(2))
        part=clip(remaining,(-normal[0],-normal[1]),-limit)
        if part:outside.append(part)
        remaining=clip(remaining,normal,limit)
        if not remaining:break
    return outside,remaining


def value(affine,point):
    return affine[0]*point[0]+affine[1]*point[1]+affine[2]


def bounds(points):
    return min(p[0] for p in points),max(p[0] for p in points),min(p[1] for p in points),max(p[1] for p in points)


def overlaps(a,b):
    return min(a[1],b[1])-max(a[0],b[0])>EPS and min(a[3],b[3])-max(a[2],b[2])>EPS


def feature_regions(feature,groove,depth,vgroove,angle):
    motif=feature["primitive"];kind=motif["kind"]
    if kind=="polygon":
        candidates=[(clean(motif["points"]),(0.,0.,depth))]
    elif kind=="ring":
        cx,cz=motif["center"];radius=motif["radius"]
        outer,inner=radius+groove/2,radius-groove/2
        number=max(32,math.ceil(math.pi/math.acos(max(-1.,1-CHORD_ERROR/outer))))
        candidates=[]
        for i in range(number):
            a,b=2*math.pi*i/number,2*math.pi*(i+1)/number
            candidates.append((clean([[cx+r*math.cos(t),cz+r*math.sin(t)]
                                      for r,t in ((inner,a),(outer,a),(outer,b),(inner,b))]),(0.,0.,depth)))
    else:
        a,b=motif["a"],motif["b"];length=math.dist(a,b)
        if length<=EPS:return []
        normal=(-(b[1]-a[1])/length,(b[0]-a[0])/length)
        center=sum(normal[i]*a[i] for i in range(2))
        def strip(lo,hi):
            return clean([[q[0]+normal[0]*offset,q[1]+normal[1]*offset]
                          for q,offset in ((a,lo),(b,lo),(b,hi),(a,hi))])
        if vgroove:
            tangent=math.tan(math.radians(angle/2))
            candidates=[(strip(-groove/2,0),(normal[0]/tangent,normal[1]/tangent,depth-center/tangent)),
                        (strip(0,groove/2),(-normal[0]/tangent,-normal[1]/tangent,depth+center/tangent))]
        else:candidates=[(strip(-groove/2,groove/2),(0.,0.,depth))]
    result=[]
    for polygon,affine in candidates:
        _,polygon=subtract_convex(polygon,rectangle(feature["boundary"]))
        pieces=[polygon] if polygon else []
        for zone in feature["protected"]:
            next_pieces=[]
            for part in pieces:
                if overlaps(bounds(part),zone):next_pieces.extend(subtract_convex(part,rectangle(zone))[0])
                else:next_pieces.append(part)
            pieces=next_pieces
        result.extend((part,affine,feature["side"]) for part in pieces)
    return result


def cells(width,height,thickness,features,groove,depth,vgroove,angle):
    result=[(rectangle((0,width,0,height)),(0.,0.,0.),(0.,0.,0.))]
    for feature in features:
        for tool,affine,side in feature_regions(feature,groove,depth,vgroove,angle):
            box=bounds(tool);updated=[]
            for polygon,front,back in result:
                if not overlaps(bounds(polygon),box):
                    updated.append((polygon,front,back));continue
                outside,inside=subtract_convex(polygon,tool)
                updated.extend((part,front,back) for part in outside)
                if not inside:continue
                old=front if side=="front" else back
                difference=[affine[i]-old[i] for i in range(3)]
                # Keep the deeper of overlapping cuts, splitting at their exact equality.
                keep=clip(inside,difference[:2],-difference[2])
                cut=clip(inside,[-v for v in difference[:2]],difference[2])
                if max(abs(v) for v in difference)<=EPS:
                    updated.append((inside,front,back));continue
                if keep:updated.append((keep,front,back))
                if cut:updated.append((cut,affine if side=="front" else front,affine if side=="back" else back))
            result=updated
    return [(poly,front,back) for poly,front,back in result
            if any(thickness-value(front,p)-value(back,p)>EPS for p in poly)]


def emit(model,key,width,height,thickness,features,groove,depth,vgroove,angle):
    faces=[]
    def face(points):
        origin=points[0]
        x=[points[1][i]-origin[i] for i in range(3)]
        length=math.hypot(*x)
        if length<=EPS:return
        x=[v/length for v in x]
        normal=None
        for p in points[2:]:
            v=[p[i]-origin[i] for i in range(3)]
            n=[x[1]*v[2]-x[2]*v[1],x[2]*v[0]-x[0]*v[2],x[0]*v[1]-x[1]*v[0]]
            size=math.hypot(*n)
            if size>EPS:normal=[v/size for v in n];break
        if normal is None:return
        y=[normal[1]*x[2]-normal[2]*x[1],normal[2]*x[0]-normal[0]*x[2],normal[0]*x[1]-normal[1]*x[0]]
        local=clean([[sum((p[i]-origin[i])*axis[i] for i in range(3)) for axis in (x,y)] for p in points])
        if not local:return
        contour={"kind":"path","closed":True,"segments":[
            {"kind":"line","start":a,"end":b} for a,b in zip(local,local[1:]+local[:1])]}
        faces.append(model.geometry(key+f".face.{len(faces)}","profile2d",arguments={
            "placement":{"origin":origin,"xAxis":x,"yAxis":y},"contours":[contour]}))
    for polygon,front,back in cells(width,height,thickness,features,groove,depth,vgroove,angle):
        a=[[p[0],-thickness/2+value(front,p),p[1]] for p in polygon]
        b=[[p[0],thickness/2-value(back,p),p[1]] for p in polygon]
        face(a);face(b[::-1])
        for i in range(len(polygon)):
            j=(i+1)%len(polygon);face([a[i],b[i],b[j],a[j]])
    if not faces:raise ValueError("门扇显示轮廓没有剩余材料")
    return model.geometry(key+".surface","compound",inputs=faces)
