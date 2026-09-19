"""Geometric measurements only. No template IDs, builds or numerical searches."""
import math


def strip_axes(loop,t):
    """Pair the two complete boundaries of a constant-thickness bent strip."""
    corners=polygon_corners(loop,t)
    if not corners or len(corners)%2:return []
    n=len(corners)//2
    if n<3:return []
    found=[]
    for shift in range(2*n):
        cs=corners[shift:]+corners[:shift]
        a=cs[:n];b=list(reversed(cs[n:]))
        if any(c[1]>t for c in (a[0],a[-1],b[0],b[-1])):continue
        wall=math.dist(a[0][0],b[0][0])
        if wall<=t or abs(math.dist(a[-1][0],b[-1][0])-wall)>t:continue
        points=[[(aa[0][d]+bb[0][d])/2 for d in (0,1)] for aa,bb in zip(a,b)]
        valid=True;directions=[]
        for i in range(n-1):
            length=math.dist(points[i],points[i+1])
            if length<=t:valid=False;break
            u=[(points[i+1][d]-points[i][d])/length for d in (0,1)];directions.append(u)
            for j in (i,i+1):
                delta=[a[j][0][d]-b[j][0][d] for d in (0,1)]
                if abs(abs(u[0]*delta[1]-u[1]*delta[0])-wall)>t:valid=False
            for side in (a,b):
                delta=[side[i+1][0][d]-side[i][0][d] for d in (0,1)]
                if abs(u[0]*delta[1]-u[1]*delta[0])>t or sum(u[d]*delta[d] for d in (0,1))<=t:valid=False
        if not valid:continue
        for j,u in ((0,directions[0]),(n-1,directions[-1])):
            if abs(sum((a[j][0][d]-b[j][0][d])*u[d] for d in (0,1)))>t:valid=False
        r=min(a[1][1],b[1][1])
        for i in range(1,n-1):
            if abs(min(a[i][1],b[i][1])-r)>t or abs(max(a[i][1],b[i][1])-r-wall)>t:valid=False
            u,v=directions[i-1],directions[i]
            delta=[a[i][0][d]-b[i][0][d] for d in (0,1)]
            side=u[0]*delta[1]-u[1]*delta[0]
            turn=u[0]*v[1]-u[1]*v[0]
            if abs(a[i][1]-(r if side*turn>0 else r+wall))>t:valid=False
        if valid:found.append((points,wall,r))
    return found


def shifted_pose(pose,point):
    c,s=math.cos(pose['rotation']),math.sin(pose['rotation'])
    return dict(rotation=pose['rotation'],translation=[pose['translation'][0]+c*point[0]-s*point[1],
        pose['translation'][1]+s*point[0]+c*point[1]])


def polygon_corners(loop,t,chamfers=False):
    """Intersect existing adjacent support lines and validate tangent fillets."""
    if loop['kind']!='path':return False
    edges=loop['edges'];indices=[i for i,e in enumerate(edges) if e['kind']=='line' and
        (not chamfers or min(abs(e['end'][0]-e['start'][0]),abs(e['end'][1]-e['start'][1]))<=t)]
    if len(indices)<3:return False
    corners=[]
    for k,i in enumerate(indices):
        j=indices[(k+1)%len(indices)]
        between=[];n=(i+1)%len(edges)
        while n!=j:
            between.append(edges[n]);n=(n+1)%len(edges)
        if len(between)>1 or any(e['kind'] not in (('arc','line') if chamfers else ('arc',)) for e in between):return False
        a,b=edges[i],edges[j]
        u=[a['end'][d]-a['start'][d] for d in (0,1)]
        v=[b['end'][d]-b['start'][d] for d in (0,1)]
        lu,lv=math.hypot(*u),math.hypot(*v)
        if min(lu,lv)<=t:return False
        u=[x/lu for x in u];v=[x/lv for x in v]
        cross=u[0]*v[1]-u[1]*v[0]
        if abs(cross)<1e-9:return False
        delta=[b['start'][d]-a['end'][d] for d in (0,1)]
        distance=(delta[0]*v[1]-delta[1]*v[0])/cross
        p=[a['end'][d]+distance*u[d] for d in (0,1)]
        r=0
        if between and between[0]['kind']=='line':
            cut=between[0]
            first,second=math.dist(p,cut['start']),math.dist(p,cut['end'])
            if abs(first-second)>t or first<=t:return False
            if distance < -t or sum((cut['end'][d]-p[d])*v[d] for d in (0,1)) < -t:return False
            if math.dist(a['end'],cut['start'])>t or math.dist(b['start'],cut['end'])>t:return False
            r=-first  # Negative tag denotes a measured straight equal-leg corner.
        elif between:
            arc=between[0];r=arc['radius']
            turn=math.atan2(cross,sum(u[d]*v[d] for d in (0,1)))
            if abs(arc['sweep']-turn)*r>t:return False
            if abs(distance-r*math.tan(abs(turn)/2))>t:return False
            for point,direction in ((arc['start'],u),(arc['end'],v)):
                radial=[point[d]-arc['center'][d] for d in (0,1)]
                if abs(sum(radial[d]*direction[d] for d in (0,1)))>t:return False
            if math.dist(a['end'],arc['start'])>t or math.dist(b['start'],arc['end'])>t:return False
        elif math.dist(a['end'],b['start'])>t:return False
        corners.append((p,r))
    return corners


def regular_polygon(loop, sides, tolerance):
    """Measure a complete straight-sided regular polygon, including its phase."""
    if loop['kind'] != 'path':return False
    edges=loop['edges']
    if len(edges)!=sides or any(e['kind']!='line' for e in edges):return False
    points=[e['start'] for e in edges]
    return regular_vertices(points,tolerance)


def regular_vertices(points,tolerance):
    sides=len(points)
    center=[sum(p[i] for p in points)/sides for i in (0,1)]
    radius=math.dist(points[0],center)
    if radius<=tolerance:return False
    angles=[math.atan2(p[1]-center[1],p[0]-center[0]) for p in points]
    for i,p in enumerate(points):
        if abs(math.dist(p,center)-radius)>tolerance:return False
        if abs((angles[(i+1)%sides]-angles[i])%math.tau-math.tau/sides)*radius>tolerance:return False
    return dict(center=center,radius=radius,size=2*radius*math.cos(math.pi/sides),
                phase=angles[0]%(math.tau/sides))

def result(parameters,pose,uniqueness="non-unique"):
    return {"parameters":parameters,"rotationDegrees":math.degrees(pose["rotation"])%360,
            "translation":pose["translation"],"rotationUniqueness":uniqueness}

def concentric_circles(loops,tolerance,count):
    if len(loops)!=count or any(c["kind"]!="circle" for c in loops):return False
    outer=loops[0]
    if outer["radius"]<=tolerance:return False
    if any(c["radius"]<=tolerance or math.dist(c["center"],outer["center"])>tolerance for c in loops):return False
    if count==2 and outer["radius"]-loops[1]["radius"]<=tolerance:return False
    return [c["radius"] for c in loops],{"rotation":0,"translation":outer["center"]}


def eccentric_circles(loops,tolerance):
    """Recognize a circular tube with an inner circle that may be offset."""
    if len(loops)!=2 or any(c["kind"]!="circle" for c in loops):return False
    outer,inner=loops
    outer_radius,inner_radius=outer["radius"],inner["radius"]
    if outer_radius<=tolerance or inner_radius<=tolerance:return False
    if inner_radius>=outer_radius-tolerance:return False
    offset=[inner["center"][i]-outer["center"][i] for i in (0,1)]
    if math.dist(offset,[0,0])+inner_radius>=outer_radius-tolerance:return False
    return (outer_radius,inner_radius,offset[0],offset[1]),{
        "rotation":0,"translation":outer["center"]}

def box(loop,g,t):
    """Recognize a complete axis-aligned rectangle with four equal corner arcs."""
    if loop["kind"]!="path":return False
    edges=loop["edges"];lines=[e for e in edges if e["kind"]=="line"]
    arcs=[e for e in edges if e["kind"]=="arc"]
    if len(lines)!=4 or len(arcs) not in (0,4):return False
    x0,y0,x1,y1=g.bounds(loop);w,h=x1-x0,y1-y0
    if min(w,h)<=t:return False
    r=arcs[0]["radius"] if arcs else 0
    if r<0 or 2*r>=min(w,h)-t:return False
    sides=set()
    for e in lines:
        a,b=e["start"],e["end"]
        if abs(a[0]-b[0])<=t:
            side=0 if abs(a[0]-x0)<=t else 1 if abs(a[0]-x1)<=t else -1
            low,high=sorted((a[1],b[1]))
            if abs(low-(y0+r))>t or abs(high-(y1-r))>t:return False
        elif abs(a[1]-b[1])<=t:
            side=2 if abs(a[1]-y0)<=t else 3 if abs(a[1]-y1)<=t else -1
            low,high=sorted((a[0],b[0]))
            if abs(low-(x0+r))>t or abs(high-(x1-r))>t:return False
        else:return False
        if side<0 or side in sides:return False
        sides.add(side)
    corners=set()
    for e in arcs:
        if abs(e["radius"]-r)>t or abs(e["sweep"]-math.pi/2)*r>t:return False
        cx,cy=e["center"]
        sx=-1 if abs(cx-(x0+r))<=t else 1 if abs(cx-(x1-r))<=t else 0
        sy=-1 if abs(cy-(y0+r))<=t else 1 if abs(cy-(y1-r))<=t else 0
        if not sx or not sy or (sx,sy) in corners:return False
        corners.add((sx,sy))
        for p in (e["start"],e["end"]):
            if abs(math.dist(p,e["center"])-r)>t or (p[0]-cx)*sx<-t or (p[1]-cy)*sy<-t:return False
    return {"width":w,"depth":h,"radius":r,"center":[(x0+x1)/2,(y0+y1)/2]}

def capsule(loop,g,t):
    if loop["kind"]!="path":return False
    lines=[e for e in loop["edges"] if e["kind"]=="line"]
    arcs=[e for e in loop["edges"] if e["kind"]=="arc"]
    if len(lines)!=2 or len(arcs)!=2:return False
    a,b=sorted(arcs,key=lambda e:e["center"][0]);r=a["radius"]
    x0,cy=a["center"];x1,by=b["center"]
    if r<=t or x1-x0<=t or abs(cy-by)>t or abs(b["radius"]-r)>t:return False
    for e,sign in ((a,-1),(b,1)):
        if abs(e["sweep"]-math.pi)*r>t:return False
        for p in (e["start"],e["end"]):
            if abs(p[0]-e["center"][0])>t or abs(abs(p[1]-cy)-r)>t:return False
        start=math.atan2(e["start"][1]-cy,e["start"][0]-e["center"][0])
        if math.cos(start+e["sweep"]/2)*sign<0:return False
    sides=set()
    for e in lines:
        p,q=e["start"],e["end"]
        side=1 if abs(p[1]-cy-r)<=t else -1 if abs(p[1]-cy+r)<=t else 0
        if not side or side in sides or abs(p[1]-q[1])>t:return False
        if abs(min(p[0],q[0])-x0)>t or abs(max(p[0],q[0])-x1)>t:return False
        sides.add(side)
    return {"width":x1-x0+2*r,"depth":2*r,"radius":r,"center":[(x0+x1)/2,cy]}

def paired_sections(loops,g,t,measure):
    if len(loops)!=2:return False
    outer,inner=measure(loops[0],g,t),measure(loops[1],g,t)
    if not outer or not inner or math.dist(outer["center"],inner["center"])>t:return False
    tx=(outer["width"]-inner["width"])/2
    ty=(outer["depth"]-inner["depth"])/2
    if tx<=t or abs(tx-ty)>t:return False
    if abs(inner["radius"]-max(outer["radius"]-tx,0))>t:return False
    return outer,tx


def paired_sections_offset(loops,g,t,measure):
    """Pair two same-family closed sections while retaining inner-center offset."""
    if len(loops)!=2:return False
    outer,inner=measure(loops[0],g,t),measure(loops[1],g,t)
    if not outer or not inner:return False
    tx=(outer["width"]-inner["width"])/2
    ty=(outer["depth"]-inner["depth"])/2
    if tx<=t or abs(tx-ty)>t:return False
    if abs(inner["radius"]-max(outer["radius"]-tx,0))>t:return False
    offset=[inner["center"][i]-outer["center"][i] for i in (0,1)]
    if max(abs(offset[0]),abs(offset[1]))>=tx-t:return False
    return outer,tx,offset


def _rounded_rectangle(loop,g,t):
    """Measure one analytic axis-aligned rounded rectangle, including mixed corners."""
    if loop.get("kind") != "path": return False
    edges = loop.get("edges", [])
    if not edges: return False
    x0,y0,x1,y1 = g.bounds(loop)
    if min(x1-x0,y1-y0) <= t: return False
    radii = [0.0,0.0,0.0,0.0]
    arc_count = 0
    line_count = 0
    for edge in edges:
        kind = edge.get("kind")
        if kind == "line":
            a,b=edge["start"],edge["end"]
            dx,dy=b[0]-a[0],b[1]-a[1]
            if math.hypot(dx,dy) <= t: return False
            horizontal=abs(dy)<=t and (abs(a[1]-y0)<=t or abs(a[1]-y1)<=t)
            vertical=abs(dx)<=t and (abs(a[0]-x0)<=t or abs(a[0]-x1)<=t)
            if not (horizontal or vertical): return False
            if horizontal and abs(b[1]-a[1])>t: return False
            if vertical and abs(b[0]-a[0])>t: return False
            line_count += 1
        elif kind == "arc":
            radius=edge.get("radius")
            center=edge.get("center")
            if not isinstance(radius,(int,float)) or radius<=t or not center:return False
            if abs(abs(edge.get("sweep",0))-math.pi/2)*radius>t:return False
            if abs(center[0]-x0)<=t or abs(center[0]-x1)<=t or abs(center[1]-y0)<=t or abs(center[1]-y1)<=t:return False
            left=abs(center[0]-(x0+radius))<=t
            right=abs(center[0]-(x1-radius))<=t
            bottom=abs(center[1]-(y0+radius))<=t
            top=abs(center[1]-(y1-radius))<=t
            if left and bottom:index=0
            elif right and bottom:index=1
            elif right and top:index=2
            elif left and top:index=3
            else:return False
            if radii[index] > 0 and abs(radii[index]-radius)>t:return False
            for point in (edge["start"],edge["end"]):
                if abs(math.dist(point,center)-radius)>t:return False
            radii[index]=float(radius);arc_count += 1
        else:return False
    if line_count != 4 or arc_count > 4 or len(edges) != 4+arc_count:return False
    # Every corner is represented by either one quarter arc or two adjacent sides.
    if arc_count and any(r<=0 for r in radii if r): pass
    center=[(x0+x1)/2,(y0+y1)/2]
    return {"width":x1-x0,"depth":y1-y0,"center":center,"radii":radii}


def paired_rectangles(loops,g,t):
    if len(loops)!=2:return False
    outer=_rounded_rectangle(loops[0],g,t); inner=_rounded_rectangle(loops[1],g,t)
    if not outer or not inner:return False
    wall_x=(outer["width"]-inner["width"])/2
    wall_y=(outer["depth"]-inner["depth"])/2
    if wall_x<=t or abs(wall_x-wall_y)>t:return False
    if abs(outer["center"][0]-((g.bounds(loops[0])[0]+g.bounds(loops[0])[2])/2))>t:return False
    return outer,inner,wall_x
