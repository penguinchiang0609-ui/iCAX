"""Profile-independent exact boundary normalization and reconstruction checks.

No template IDs or profile families belong here. Circle/ellipse parameters and
line/arc boundaries are compared analytically, not as a tessellated outline.
Different spline representations are deliberately not guessed equivalent.
"""
import copy
import math


class UnsupportedGeometry(ValueError):
    pass


def point(p):
    if not isinstance(p, (list, tuple)) or len(p) != 2:
        raise ValueError("截面坐标必须是二维点")
    if any(isinstance(x, bool) or not isinstance(x, (int,float)) or not math.isfinite(x) for x in p):
        raise ValueError("截面坐标必须是有限数值")
    return list(p)


def arc(start, middle, end):
    start,middle,end = map(point,(start,middle,end))
    bx,by = middle[0]-start[0],middle[1]-start[1]
    cx,cy = end[0]-start[0],end[1]-start[1]
    d = 2*(bx*cy-by*cx)
    if abs(d) < 1e-15:
        raise ValueError("截面含退化圆弧")
    center = [start[0]+(cy*(bx*bx+by*by)-by*(cx*cx+cy*cy))/d,
              start[1]+(bx*(cx*cx+cy*cy)-cx*(bx*bx+by*by))/d]
    radius = math.dist(start,center)
    a,m,b = [math.atan2(p[1]-center[1],p[0]-center[0]) for p in (start,middle,end)]
    sweep = (b-a)%math.tau if (m-a)%math.tau <= (b-a)%math.tau else -((a-b)%math.tau)
    return dict(kind="arc",start=start,end=end,center=center,radius=radius,sweep=sweep)


def _reverse(edge):
    result = copy.deepcopy(edge)
    result["start"],result["end"] = edge["end"],edge["start"]
    if edge["kind"] == "arc": result["sweep"] = -edge["sweep"]
    return result


def _area(edges):
    area = 0
    for e in edges:
        a,b = e["start"],e["end"]
        area += a[0]*b[1]-a[1]*b[0]
        if e["kind"] == "arc":
            area += e["radius"]**2*(e["sweep"]-math.sin(e["sweep"]))
    return area/2


def _join(edges, tolerance):
    # Remove harmless segmentation (collinear lines / co-circular arcs).
    changed = True
    while changed and len(edges)>1:
        changed = False
        for i,a in enumerate(edges):
            j = (i+1)%len(edges); b = edges[j]
            if math.dist(a["end"],b["start"]) > tolerance:
                raise ValueError("截面边界不闭合")
            same = False
            if a["kind"] == b["kind"] == "line":
                u = [a["end"][k]-a["start"][k] for k in (0,1)]
                v = [b["end"][k]-b["start"][k] for k in (0,1)]
                same = abs(u[0]*v[1]-u[1]*v[0]) <= 1e-10*max(1,math.hypot(*u)*math.hypot(*v)) and sum(x*y for x,y in zip(u,v))>0
            elif a["kind"] == b["kind"] == "arc":
                same = (math.dist(a["center"],b["center"]) <= 1e-9
                        and abs(a["radius"]-b["radius"]) <= 1e-9 and a["sweep"]*b["sweep"]>0)
            if same:
                merged = {**a,"end":b["end"]}
                if a["kind"] == "arc": merged["sweep"] += b["sweep"]
                edges = ([merged]+edges[1:i] if j==0 else edges[:i]+[merged]+edges[j+1:])
                changed=True
                break
    if len(edges)==1 and edges[0]["kind"]=="arc" and abs(abs(edges[0]["sweep"])-math.tau)<1e-9:
        e=edges[0]
        return {"kind":"circle","center":e["center"],"radius":e["radius"]}
    if _area(edges)<0: edges=[_reverse(e) for e in reversed(edges)]
    return {"kind":"path","edges":edges}


def contour(value, tolerance):
    kind=value.get("kind")
    center=point(value.get("center",[0,0]))
    if kind=="circle": return {"kind":"circle","center":center,"radius":float(value["radius"])}
    if kind=="ellipse":
        a,b=value["width"]/2,value["height"]/2
        angle=float(value.get("rotation",0))
        if a<b: a,b,angle=b,a,angle+math.pi/2
        if abs(a-b)<1e-12:return {"kind":"circle","center":center,"radius":a}
        return {"kind":"ellipse","center":center,"a":a,"b":b,"angle":angle%math.pi}
    if kind in ("roundedRectangle","capsule"):
        w,h=float(value["width"]),float(value["height"])
        r=min(w,h)/2 if kind=="capsule" else float(value.get("radius",0))
        if r<0 or r>min(w,h)/2:raise ValueError("圆角半径无效")
        x,y=w/2,h/2
        centers=[[-x+r,-y+r],[x-r,-y+r],[x-r,y-r],[-x+r,y-r]]
        edges=[]
        for i,c in enumerate(centers):
            a=math.pi+i*math.pi/2
            start=[c[0]+r*math.cos(a),c[1]+r*math.sin(a)]
            end=[c[0]+r*math.cos(a+math.pi/2),c[1]+r*math.sin(a+math.pi/2)]
            if r:edges.append(dict(kind="arc",center=c,radius=r,start=start,end=end,sweep=math.pi/2))
            nxt=centers[(i+1)%4]
            next_start=[nxt[0]+r*math.cos(a+math.pi/2),nxt[1]+r*math.sin(a+math.pi/2)]
            if math.dist(end,next_start)>1e-10:edges.append(dict(kind="line",start=end,end=next_start))
        for e in edges:
            for k in ("start","end","center"):
                if k in e:e[k]=[e[k][j]+center[j] for j in (0,1)]
        return _join(edges,tolerance)
    if kind=="polygon":
        pts=[point(p) for p in value["points"]]
        if len(pts)>1 and math.dist(pts[0],pts[-1])<1e-10:pts.pop()
        return _join([dict(kind="line",start=a,end=b) for a,b in zip(pts,pts[1:]+pts[:1])],tolerance)
    raw=value.get("segments",value.get("edges"))
    if not isinstance(raw,list) or not raw:raise UnsupportedGeometry("逆向查询不支持此轮廓表达")
    if all(e.get("kind") == "ellipseArc" for e in raw):
        first = raw[0]
        center = point(first["center"])
        major = float(first["majorRadius"])
        minor = float(first["minorRadius"])
        rotation = float(first.get("rotation", 0.0))
        total = 0.0
        for e in raw:
            if (point(e["center"]) != center
                    or abs(float(e["majorRadius"]) - major) > tolerance
                    or abs(float(e["minorRadius"]) - minor) > tolerance
                    or abs(float(e.get("rotation", 0.0)) - rotation) > tolerance):
                raise UnsupportedGeometry("同一椭圆的 ellipseArc 参数必须一致")
            total += float(e["endAngle"]) - float(e["startAngle"])
        if abs(abs(total) - math.tau) <= tolerance:
            return contour(dict(kind="ellipse", center=center,
                                width=2 * major, height=2 * minor,
                                rotation=rotation), tolerance)
    edges=[]
    for e in raw:
        k=e["kind"]
        if k=="line":edges.append(dict(kind=k,start=point(e["start"]),end=point(e["end"])))
        elif k=="arc":edges.append(arc(e["start"],e["middle"],e["end"]))
        elif k=="circleArc":
            a,b=e["first"],e["last"]
            sweep=b-a
            if "counterClockwise" in e and abs(sweep)<math.tau-1e-9:
                sweep=(b-a)%math.tau if e["counterClockwise"] else -((a-b)%math.tau)
            edges.append(dict(kind="arc",start=point(e["start"]),end=point(e["end"]),
                              center=point(e["center"]),radius=e["radius"],sweep=sweep))
        elif k=="ellipseArc" and abs(abs(e.get("endAngle",e.get("last",0))-e.get("startAngle",e.get("first",0)))-math.tau)<1e-9:
            if len(raw)!=1:raise ValueError("完整椭圆不能附带其他边")
            angle=e.get("rotation",math.atan2(e.get("xAxis",[1,0])[1],e.get("xAxis",[1,0])[0]))
            return contour(dict(kind="ellipse",center=e["center"],width=2*e["majorRadius"],height=2*e["minorRadius"],rotation=angle),tolerance)
        else:raise UnsupportedGeometry("此精确曲线尚无逆向等价校验，不能按采样结果认定匹配："+k)
    return _join(edges,tolerance)


def normalize(section,tolerance):
    if not isinstance(section,dict) or not isinstance(section.get("contours"),list) or not 1<=len(section["contours"])<=1000:
        raise ValueError("逆向识别需要非空完整截面")
    result=[contour(c,tolerance) for c in section["contours"]]
    return sorted(result,key=lambda c:-(bounds(c)[2]-bounds(c)[0])*(bounds(c)[3]-bounds(c)[1]))


def bounds(c):
    if c["kind"]=="circle":
        x,y=c["center"];r=c["radius"]
        return [x-r,y-r,x+r,y+r]
    if c["kind"]=="ellipse":
        x,y=c["center"];a,b,t=c["a"],c["b"],c["angle"]
        dx,dy=math.hypot(a*math.cos(t),b*math.sin(t)),math.hypot(a*math.sin(t),b*math.cos(t))
        return [x-dx,y-dy,x+dx,y+dy]
    pts=[]
    for e in c["edges"]:
        pts.extend((e["start"],e["end"]))
        if e["kind"]=="arc":
            a=math.atan2(e["start"][1]-e["center"][1],e["start"][0]-e["center"][0])
            for t in (0,math.pi/2,math.pi,3*math.pi/2):
                delta=(t-a)%math.tau if e["sweep"]>0 else (a-t)%math.tau
                if delta<=abs(e["sweep"])+1e-12:
                    pts.append([e["center"][0]+e["radius"]*math.cos(t),e["center"][1]+e["radius"]*math.sin(t)])
    return [min(p[0] for p in pts),min(p[1] for p in pts),max(p[0] for p in pts),max(p[1] for p in pts)]


def transform(loops,angle,translation):
    result=copy.deepcopy(loops);c,s=math.cos(angle),math.sin(angle)
    def move(p):return [c*p[0]-s*p[1]+translation[0],s*p[0]+c*p[1]+translation[1]]
    for loop in result:
        if loop["kind"] in ("circle","ellipse"):
            loop["center"]=move(loop["center"])
            if loop["kind"]=="ellipse":loop["angle"]=(loop["angle"]+angle)%math.pi
        else:
            for e in loop["edges"]:
                for key in ("start","end","center"):
                    if key in e:e[key]=move(e[key])
    return result


def to_section(loops):
    """Return neutral-model contours, preserving analytic arcs and ellipses."""
    contours=[]
    for loop in loops:
        kind=loop["kind"]
        if kind=="ellipse":
            contours.append({"kind":"path","closed":True,"segments":[{
                "kind":"ellipseArc","center":loop["center"],"majorRadius":loop["a"],
                "minorRadius":loop["b"],"rotation":loop["angle"],"startAngle":0,"endAngle":math.tau}]})
            continue
        edges=loop.get("edges",[])
        if kind=="circle":
            center,r=loop["center"],loop["radius"]
            edges=[dict(kind="arc",center=center,radius=r,sweep=math.pi/2,
                        start=[center[0]+r*math.cos(a),center[1]+r*math.sin(a)],
                        end=[center[0]+r*math.cos(a+math.pi/2),center[1]+r*math.sin(a+math.pi/2)])
                   for a in (0,math.pi/2,math.pi,3*math.pi/2)]
        segments=[]
        for e in edges:
            if e["kind"]=="line":segments.append(copy.deepcopy(e));continue
            a=math.atan2(e["start"][1]-e["center"][1],e["start"][0]-e["center"][0])+e["sweep"]/2
            segments.append(dict(kind="arc",start=e["start"],end=e["end"],
                middle=[e["center"][0]+e["radius"]*math.cos(a),e["center"][1]+e["radius"]*math.sin(a)]))
        contours.append(dict(kind="path",closed=True,segments=segments))
    return {"contours":contours}


def frames(loops):
    angles=[0.0]
    pts=[e["start"] for e in loops[0].get("edges",[])]
    if pts:
        center=[sum(p[i] for p in pts)/len(pts) for i in (0,1)]
        xx=sum((p[0]-center[0])**2 for p in pts)
        yy=sum((p[1]-center[1])**2 for p in pts)
        xy=sum((p[0]-center[0])*(p[1]-center[1]) for p in pts)
        angles.append(math.atan2(2*xy,xx-yy)/2)
    for loop in loops:
        if loop["kind"]=="ellipse":angles.append(loop["angle"])
        for e in loop.get("edges",[]):
            if e["kind"]=="line":
                angles.append(math.atan2(e["end"][1]-e["start"][1],e["end"][0]-e["start"][0]))
    seen=[]
    for base in angles:
        for angle in (base,base+math.pi/2,base+math.pi,base+3*math.pi/2):
            angle=angle%math.tau
            if any(abs(math.remainder(angle-a,math.tau))<1e-8 for a in seen):continue
            seen.append(angle)
            local=transform(loops,-angle,[0,0]);b=bounds(local[0])
            center=[(b[0]+b[2])/2,(b[1]+b[3])/2]
            local=transform(local,0,[-v for v in center])
            c,s=math.cos(angle),math.sin(angle)
            yield local,dict(rotation=angle,translation=[c*center[0]-s*center[1],s*center[0]+c*center[1]])
            if len(seen)>=64:return


def _edge_vector(e):
    values=e["start"]+e["end"]
    if e["kind"]=="arc":
        values+=e["center"]+[e["radius"],e["radius"]*e["sweep"]]
    return values


def loop_residual(a,b):
    if a["kind"]!=b["kind"]:return None
    if a["kind"] in ("circle","ellipse"):
        av,bv=a["center"][:],b["center"][:]
        if a["kind"]=="circle":av+=[a["radius"]];bv+=[b["radius"]]
        else:
            # A symmetric quadratic tensor avoids the pi-periodic axis ambiguity.
            for c,v in ((a,av),(b,bv)):
                x,y,t=c["a"],c["b"],c["angle"]
                v.extend([x,y,(x-y)*math.cos(2*t),(x-y)*math.sin(2*t)])
        return [x-y for x,y in zip(av,bv)]
    ae,be=a["edges"],b["edges"]
    if len(ae)!=len(be):return None
    best=None;score=math.inf
    for shift in range(len(be)):
        target=be[shift:]+be[:shift]
        if any(x["kind"]!=y["kind"] for x,y in zip(ae,target)):continue
        values=[x-y for e,f in zip(ae,target) for x,y in zip(_edge_vector(e),_edge_vector(f))]
        cost=sum(v*v for v in values)
        if cost<score:best,score=values,cost
    return best


def residual(a,b):
    if len(a)!=len(b):return None
    # Inner-loop ordering is not a part of section identity. Deterministic
    # minimum-cost assignment is used for candidate fitting; final checks still
    # compare every boundary, including hole locations and sizes.
    remaining=list(b);result=[]
    for loop in a:
        choices=[(sum(v*v for v in r),i,r) for i,c in enumerate(remaining)
                 if (r:=loop_residual(loop,c)) is not None]
        if not choices:return None
        _,i,r=min(choices,key=lambda item:item[0]);remaining.pop(i);result.extend(r)
    return result


def verify(original,rebuilt,pose,tolerance):
    moved=transform(rebuilt,pose["rotation"],pose["translation"])
    values=residual(original,moved)
    error=max(map(abs,values),default=0) if values is not None else math.inf
    return error<=tolerance,error
