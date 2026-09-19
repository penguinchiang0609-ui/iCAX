"""P30 多边形棒：族内模型独立实现。"""
"""Generic exact section construction, with no market-name dispatch.

Geometry helpers are kept in this profile.py so the template remains self-contained.
Lengths are millimetres; polygon fillets are circular arcs, not tessellations.
"""
import copy
import math
import json


def positive(*values):
    if any(not math.isfinite(v) or v <= 1e-6 for v in values):
        raise ValueError("尺寸必须为大于零的有限数值")


def radii(text,count,default=0):
    values=[float(s.strip()) for s in text.replace("，",",").split(",")] if text.strip() else [default]*count
    if len(values)!=count or any(not math.isfinite(v) or v<0 for v in values):
        raise ValueError(f"圆角须为 {count} 个非负数，按轮廓顶点顺序填写")
    return values


def polygon(points,corner_radii=None):
    points=[list(p) for p in points]
    if len(points)<3:raise ValueError("轮廓至少需要三个顶点")
    rs=corner_radii or [0]*len(points)
    if len(rs)!=len(points):raise ValueError("圆角数量不匹配")
    if not any(rs):
        return {"kind":"path","closed":True,"segments":[
            {"kind":"line","start":points[i],"end":points[(i+1)%len(points)]}
            for i in range(len(points))
        ]}
    corners=[]
    for i,p in enumerate(points):
        before,after=points[i-1],points[(i+1)%len(points)]
        u=[p[j]-before[j] for j in (0,1)];v=[after[j]-p[j] for j in (0,1)]
        lu,lv=math.hypot(*u),math.hypot(*v);positive(lu,lv)
        u=[x/lu for x in u];v=[x/lv for x in v]
        turn=math.atan2(u[0]*v[1]-u[1]*v[0],sum(a*b for a,b in zip(u,v)))
        r=rs[i]
        if r<0 or not math.isfinite(r):raise ValueError("圆角半径无效")
        d=r*math.tan(abs(turn)/2)
        if d>=min(lu,lv)-1e-8:raise ValueError("圆角超过相邻直边长度")
        start=[p[j]-u[j]*d for j in (0,1)];end=[p[j]+v[j]*d for j in (0,1)]
        sign=1 if turn>0 else -1
        center=[start[0]-u[1]*sign*r,start[1]+u[0]*sign*r]
        angle=math.atan2(start[1]-center[1],start[0]-center[0])+turn/2
        middle=[center[0]+r*math.cos(angle),center[1]+r*math.sin(angle)]
        corners.append((start,end,middle,d,r))
    segments=[]
    for i,(start,end,middle,d,r) in enumerate(corners):
        previous=corners[i-1]
        if previous[3]+d>=math.dist(points[i-1],points[i])-1e-8:raise ValueError("相邻圆角重叠")
        segments.append({"kind":"line","start":previous[1],"end":start})
        if r:segments.append({"kind":"arc","start":start,"middle":middle,"end":end})
    return {"kind":"path","closed":True,"segments":segments}


def box(width,height,x=0,y=0,rs=None):
    positive(width,height)
    return polygon([[x-width/2,y-height/2],[x+width/2,y-height/2],
                    [x+width/2,y+height/2],[x-width/2,y+height/2]],rs)


def corner_polygon(points, rs, definition=""):
    """Mixed sharp/arc/chamfer/custom Bezier corners; dimensions are local mm.

    Custom poles use incoming/outgoing tangent axes with the vertex at (0,0).
    Poles must progress monotonically from (-incoming,0) to (0,outgoing).
    """
    if not definition.strip():return polygon(points,rs)
    specs=json.loads(definition)
    if not isinstance(specs,list) or len(specs)!=len(points):raise ValueError("角部定义数量必须与顶点一致")
    corners=[]
    for i,p in enumerate(points):
        before,after=points[i-1],points[(i+1)%len(points)]
        lu,lv=math.dist(before,p),math.dist(p,after);positive(lu,lv)
        u=[(p[k]-before[k])/lu for k in (0,1)];v=[(after[k]-p[k])/lv for k in (0,1)]
        spec=specs[i]
        if not isinstance(spec,dict):raise ValueError("每个角部必须为对象")
        mode=spec.get("mode","arc");r=spec.get("radius",rs[i])
        turn=math.atan2(u[0]*v[1]-u[1]*v[0],sum(u[k]*v[k] for k in (0,1)))
        if mode=="sharp":a=b=0
        elif mode=="arc":
            if not math.isfinite(r) or r<0:raise ValueError("圆角半径无效")
            a=b=r*math.tan(abs(turn)/2)
        elif mode in ("chamfer","custom"):
            a=spec.get("incoming",r);b=spec.get("outgoing",r);positive(a,b)
        else:raise ValueError("角部模式须为 sharp/arc/chamfer/custom")
        if a>=lu or b>=lv:raise ValueError("角部超过相邻边长")
        start=[p[k]-u[k]*a for k in (0,1)];end=[p[k]+v[k]*b for k in (0,1)]
        edge=None
        if mode=="arc" and r:
            sign=1 if turn>0 else -1
            center=[start[0]-u[1]*sign*r,start[1]+u[0]*sign*r]
            angle=math.atan2(start[1]-center[1],start[0]-center[0])+turn/2
            edge={"kind":"arc","start":start,"middle":[center[0]+r*math.cos(angle),center[1]+r*math.sin(angle)],"end":end}
        elif mode=="chamfer":edge={"kind":"line","start":start,"end":end}
        elif mode=="custom":
            poles=spec.get("controlPoints",[[-a,0],[0,0],[0,b]])
            if not isinstance(poles,list) or not 3<=len(poles)<=26:raise ValueError("自定义角部需3至26个控制点")
            if any(not isinstance(q,list) or len(q)!=2 or any(not isinstance(x,(int,float)) or not math.isfinite(x) for x in q) for q in poles):raise ValueError("控制点必须为有限二维坐标")
            if poles[0]!=[-a,0] or poles[-1]!=[0,b]:raise ValueError("控制点端点须与角部截距一致")
            if any(not -a<=x<=0 or not 0<=y<=b for x,y in poles) or any(poles[j][k]>poles[j+1][k] for j in range(len(poles)-1) for k in (0,1)):raise ValueError("自定义角部控制点须在角区内单调前进")
            edge={"kind":"bezier","controlPoints":[[p[k]+q[0]*u[k]+q[1]*v[k] for k in (0,1)] for q in poles]}
        corners.append((start,end,a,b,edge))
    segments=[]
    for i,c in enumerate(corners):
        previous=corners[i-1]
        if previous[3]+c[2]>=math.dist(points[i-1],points[i])-1e-8:raise ValueError("相邻角部重叠")
        segments.append({"kind":"line","start":previous[1],"end":c[0]})
        if c[4]:segments.append(c[4])
    return {"kind":"path","closed":True,"segments":segments}


def chamfer(points,legs):
    """Explicit straight weld-leg envelopes, never substituted with rolled fillets."""
    result=[]
    for i,p in enumerate(points):
        leg=legs.get(i,0)
        if not leg:result.append(p);continue
        for q in (points[i-1],points[(i+1)%len(points)]):
            length=math.dist(p,q)
            if leg<0 or 2*leg>=length:raise ValueError("焊脚超过相邻板段长度")
            result.append([p[j]+leg*(q[j]-p[j])/length for j in (0,1)])
    return result


def regular(sides,across_flats,phase=0):
    positive(across_flats)
    radius=across_flats/(2*math.cos(math.pi/sides))
    return [[radius*math.cos(phase+math.tau*i/sides),radius*math.sin(phase+math.tau*i/sides)] for i in range(sides)]


def offset(points,distance):
    """Intersection of inward-offset lines of a CCW convex polygon."""
    shifted=[]
    for a,b in zip(points,points[1:]+points[:1]):
        u=[b[0]-a[0],b[1]-a[1]];length=math.hypot(*u);positive(length)
        shifted.append(([a[0]-distance*u[1]/length,a[1]+distance*u[0]/length],u))
    result=[]
    for i,(b,v) in enumerate(shifted):
        a,u=shifted[i-1];d=u[0]*v[1]-u[1]*v[0]
        if abs(d)<1e-12:raise ValueError("偏置轮廓含平行相邻边")
        f=((b[0]-a[0])*v[1]-(b[1]-a[1])*v[0])/d
        result.append([a[0]+f*u[0],a[1]+f*u[1]])
    return result


def strip(points,thickness,inner_radius):
    """Constant-thickness cold-bent strip with square free ends.

    Points are theoretical centreline intersections, not finished outside sizes.
    The two bend radii are inner_radius and inner_radius + thickness.
    """
    positive(thickness)
    if inner_radius<0:raise ValueError("内弯半径不能为负数")
    directions=[]
    for a,b in zip(points,points[1:]):
        u=[b[0]-a[0],b[1]-a[1]];length=math.hypot(*u);positive(length)
        directions.append([x/length for x in u])
    sides=[];corner_radii=[]
    for sign in (1,-1):
        vertices=[];rs=[]
        for i,p in enumerate(points):
            a=directions[max(0,i-1)];b=directions[min(i,len(directions)-1)]
            if i in (0,len(points)-1):
                vertices.append([p[0]-sign*b[1]*thickness/2,p[1]+sign*b[0]*thickness/2]);rs.append(0);continue
            cross=a[0]*b[1]-a[1]*b[0]
            if abs(cross)<1e-10:raise ValueError("折弯中心线相邻段不能共线")
            dot=a[0]*b[0]+a[1]*b[1]
            shift=sign*thickness/2/(1+dot)
            vertices.append([p[0]-shift*(a[1]+b[1]),p[1]+shift*(a[0]+b[0])])
            rs.append(inner_radius if sign*cross>0 else inner_radius+thickness)
        sides.append(vertices);corner_radii.append(rs)
    return polygon(sides[0]+list(reversed(sides[1])),corner_radii[0]+list(reversed(corner_radii[1])))


def swap(contours,enabled):
    result=copy.deepcopy(contours)
    if not enabled:return result
    for c in result:
        if c["kind"] in ("ellipse","capsule","roundedRectangle"):
            c["width"],c["height"]=c["height"],c["width"]
        elif c["kind"]=="polygon":c["points"]=[list(reversed(p)) for p in c["points"]]
        elif c["kind"]=="path":
            for e in c["segments"]:
                if e["kind"]=="ellipseArc":
                    e["rotation"]=math.pi/2-e.get("rotation",0)
                    e["startAngle"],e["endAngle"]=-e["startAngle"],-e["endAngle"]
                for key in ("start","middle","end","center"):
                    if key in e:e[key]=list(reversed(e[key]))
                for key in ("controlPoints", "poles"):
                    if key in e:e[key]=[list(reversed(p)) for p in e[key]]
    return result


def envelope(contours):
    """Exact envelope for this constructor's line/arc/primitive output."""

    if not contours: raise ValueError("截面边界不能为空")
    converted=[]
    for contour in contours:
        if contour["kind"] != "path":
            converted.append(contour)
            continue
        ordinary=[]
        for edge in contour["segments"]:
            if edge["kind"] in ("bezier", "bspline", "nurbs"):
                x=spline_support(edge,[1,0]); y=spline_support(edge,[0,1])
                converted.append({"kind":"polygon","points":[[x[0],y[0]],[x[1],y[1]]]})
            elif edge["kind"] == "ellipseArc":
                a,b=edge["majorRadius"],edge["minorRadius"];positive(a,b)
                rotation=edge.get("rotation",0);c=edge.get("center",[0,0])
                u=[a*math.cos(rotation),a*math.sin(rotation)]
                v=[-b*math.sin(rotation),b*math.cos(rotation)]
                start,end=edge["startAngle"],edge["endAngle"];sweep=end-start
                angles=[start,end]
                for axis in (0,1):
                    t=math.atan2(v[axis],u[axis])
                    for q in (t,t+math.pi):
                        delta=(q-start)%math.tau if sweep>=0 else (start-q)%math.tau
                        if delta<=abs(sweep)+1e-12:angles.append(q)
                converted.append({"kind":"polygon","points":[[c[i]+u[i]*math.cos(t)+v[i]*math.sin(t) for i in (0,1)] for t in angles]})
            elif edge["kind"] in ("line", "arc"):
                ordinary.append(edge)
            else:
                raise ValueError("该曲线尚未提供包络计算，不能静默忽略")
        if ordinary:converted.append({"kind":"path","segments":ordinary})
    return _legacy_envelope(converted)


def _legacy_envelope(contours):
    points=[]
    for c in contours:
        kind=c["kind"]
        if kind=="circle":
            r=c["radius"];points.extend([[-r,-r],[r,r]])
        elif kind in ("ellipse","capsule","roundedRectangle"):
            points.extend([[-c["width"]/2,-c["height"]/2],[c["width"]/2,c["height"]/2]])
        elif kind=="polygon":points.extend(c["points"])
        else:
            for e in c["segments"]:
                points.extend([e["start"],e["end"]])
                if e["kind"]=="arc":
                    a,m,b=e["start"],e["middle"],e["end"]
                    bx,by=m[0]-a[0],m[1]-a[1];cx,cy=b[0]-a[0],b[1]-a[1]
                    d=2*(bx*cy-by*cx)
                    center=[a[0]+(cy*(bx*bx+by*by)-by*(cx*cx+cy*cy))/d,
                            a[1]+(bx*(cx*cx+cy*cy)-cx*(bx*bx+by*by))/d]
                    radius=math.dist(a,center)
                    start,mid,end=[math.atan2(p[1]-center[1],p[0]-center[0]) for p in (a,m,b)]
                    ccw=(mid-start)%math.tau<=(end-start)%math.tau
                    for t in (0,math.pi/2,math.pi,3*math.pi/2):
                        if ((t-start)%math.tau<=(end-start)%math.tau+1e-10 if ccw else (start-t)%math.tau<=(start-end)%math.tau+1e-10):
                            points.append([center[0]+radius*math.cos(t),center[1]+radius*math.sin(t)])
    lo=[min(p[i] for p in points) for i in (0,1)];hi=[max(p[i] for p in points) for i in (0,1)]
    return hi[0]-lo[0],hi[1]-lo[1]


class SectionError(ValueError):
    pass


def number(value):
    if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value):
        raise SectionError("截面几何包含无效数值")
    return float(value)


def point(value):
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise SectionError("截面点必须为二维坐标")
    return [number(v) for v in value]


def _projected_spline_spans(segment, direction):
    """Bound exact rational spans using homogeneous Bezier subdivision.

    Bounds converge by the positive-weight convex hull property, not by
    sampling. The original curve is never replaced with a polyline.
    """
    poles = [point(p) for p in segment.get("controlPoints", segment.get("poles", []))]
    degree = len(poles)-1 if segment["kind"] == "bezier" else segment.get("degree")
    if type(degree) is not int or not 1 <= degree <= 25 or len(poles) < degree+1:
        raise SectionError("样条次数或控制点数量无效")
    weights = [number(w) for w in segment.get("weights", [1.0]*len(poles))]
    if len(weights) != len(poles) or min(weights) <= 0:
        raise SectionError("样条权重必须与控制点数量一致且为正数")
    direction = point(direction)
    origin = sum(poles[0][i]*direction[i] for i in (0,1))
    scale = max(weights)
    controls = [[sum((p[i]-poles[0][i])*direction[i] for i in (0,1))*w/scale, w/scale]
                for p,w in zip(poles,weights)]
    if segment["kind"] == "bezier":
        knots = [0.0]*(degree+1)+[1.0]*(degree+1)
    else:
        knots = [number(k) for k in segment.get("knots", [])]
        mults = segment.get("multiplicities")
        if mults is not None:
            if len(mults) != len(knots) or any(type(m) is not int or m < 1 for m in mults):
                raise SectionError("样条节点重数无效")
            knots = [k for k,m in zip(knots,mults) for _ in range(m)]
    if not knots or any(a > b for a,b in zip(knots,knots[1:])):
        raise SectionError("样条节点无效")
    if segment.get("periodic"):
        n = len(controls)
        m = next((i for i,k in enumerate(knots) if k != knots[0]), 0)
        tail = sum(k == knots[-1] for k in knots)
        if not 1 <= m <= degree or tail != m or len(knots)-m != n:
            raise SectionError("周期样条节点与控制点数量不一致")
        low, high = knots[0], knots[-1]
        period = high-low
        if period <= 0:
            raise SectionError("周期样条周期无效")
        base = knots[:n]
        def knot(index):
            cycle, offset = divmod(index,n)
            return base[offset]+cycle*period
        knots = [knot(i) for i in range(-(degree+1-m), n+m+degree)]
        controls += controls[:degree]
    else:
        if len(knots) != len(controls)+degree+1:
            raise SectionError("样条节点与控制点数量不一致")
        low, high = knots[degree], knots[len(controls)]
    if ("startParameter" in segment) != ("endParameter" in segment):
        raise SectionError("样条裁剪须同时提供起止参数")
    first = number(segment.get("startParameter", segment.get("first", low)))
    last = number(segment.get("endParameter", segment.get("last", high)))
    first, last = sorted((first,last))
    if first < low-1e-10 or last > high+1e-10 or last <= first:
        raise SectionError("样条裁剪参数超出定义域")

    def linear_product(values, a, b):
        d = len(values)
        return [(a*(d-j)/d*values[j] if j < d else 0)
                +(b*j/d*values[j-1] if j else 0) for j in range(d+1)]

    def split(values, t):
        left, right = [values[0]], [values[-1]]
        while len(values) > 1:
            values = [[(1-t)*a[k]+t*b[k] for k in (0,1)] for a,b in zip(values,values[1:])]
            left.append(values[0]); right.append(values[-1])
        return left, list(reversed(right))

    spans = []
    for span in range(degree,len(controls)):
        a,b = knots[span:span+2]
        if b <= a or b <= first or a >= last:
            continue
        # Cox-de Boor basis expressed in Bernstein form on this knot span.
        basis = {span:[1.0]}
        for r in range(1,degree+1):
            next_basis = {}
            for i in range(span-r,span+1):
                values = [0.0]*(r+1)
                for index,den,x,y in ((i,knots[i+r]-knots[i],a-knots[i],b-knots[i]),
                                     (i+1,knots[i+r+1]-knots[i+1],knots[i+r+1]-a,knots[i+r+1]-b)):
                    if den and index in basis:
                        term = linear_product(basis[index],x/den,y/den)
                        values = [v+t for v,t in zip(values,term)]
                next_basis[i] = values
            basis = next_basis
        bezier = [[sum(values[j]*controls[i][k] for i,values in basis.items())
                   for k in (0,1)] for j in range(degree+1)]
        end = min(1.0,(last-a)/(b-a))
        start = max(0.0,(first-a)/(b-a))
        if end < 1:
            bezier = split(bezier,end)[0]
        if start > 0:
            bezier = split(bezier,start/end)[1]
        spans.append(bezier)
    if not spans:
        raise SectionError("样条没有有效区间")
    return origin, spans


def spline_bezier_spans(segment):
    """Exact, clamped rational spans suitable for lossless DXF export."""
    ox, xs = _projected_spline_spans(segment,[1,0])
    oy, ys = _projected_spline_spans(segment,[0,1])
    return [{"kind":"bezier", "controlPoints":[[ox+x[0]/x[1],oy+y[0]/y[1]]
             for x,y in zip(xspan,yspan)], "weights":[x[1] for x in xspan]}
            for xspan,yspan in zip(xs,ys)]


def spline_support(segment, direction, tolerance=1e-12):
    """Control-hull envelope converged to tolerance, without tessellation."""
    origin, spans = _projected_spline_spans(segment,direction)
    def split(values):
        left, right = [values[0]], [values[-1]]
        while len(values) > 1:
            values = [[(a[k]+b[k])/2 for k in (0,1)] for a,b in zip(values,values[1:])]
            left.append(values[0]); right.append(values[-1])
        return left,list(reversed(right))
    endpoints = [p[0]/p[1] for span in spans for p in (span[0],span[-1])]
    minimum, maximum = min(endpoints), max(endpoints)
    lower, upper = math.inf, -math.inf
    count = 0
    while spans:
        span = spans.pop()
        values = [p[0]/p[1] for p in span]
        if min(values) >= minimum-tolerance and max(values) <= maximum+tolerance:
            lower, upper = min(lower,min(values)), max(upper,max(values))
            continue
        left,right = split(span)
        midpoint = left[-1][0]/left[-1][1]
        minimum,maximum = min(minimum,midpoint),max(maximum,midpoint)
        spans.extend((left,right))
        count += 1
        if count > 100000:
            raise SectionError("样条极值查询未能在容差内收敛")
    return origin+lower, origin+upper




def build(p):
    p=dict(p)
    sides=p.get("sideCount")
    if sides is None:
        sides=6 if p.get("sectionModel","hexagonal-bar")=="hexagonal-bar" else 8
    if isinstance(sides,bool) or not isinstance(sides,(int,float)) or int(sides)!=sides:
        raise ValueError("边数必须为整数")
    sides=int(sides)
    if not 3<=sides<=32:raise ValueError("边数必须在 3 到 32 之间")
    radius=p.get("radius")
    if radius is None:
        radius=float(p.get("width",30))/(2*math.cos(math.pi/sides))
    radius=float(radius);corner_radius=float(p.get("cornerRadius",0))
    positive(radius)
    if not math.isfinite(corner_radius) or corner_radius<0:
        raise ValueError("圆角 R 必须为非负有限数值")
    actual=str(p.get("materialBoundary","") or "").strip()
    if actual:
        if not str(p.get("sourceRevision","") or "").strip(): raise ValueError("实际轮廓必须填写来源及版本")
        curves=json.loads(actual)
        if not isinstance(curves,list) or not curves: raise ValueError("材料边界必须为非空轮廓数组")
        w,h=envelope(curves)
        result={"width":w,"depth":h,"wallThickness":0,"cornerRadius":corner_radius,"contours":curves}
    else:
        points=[[radius*math.cos(math.tau*i/sides),radius*math.sin(math.tau*i/sides)] for i in range(sides)]
        curves=[polygon(points,[corner_radius]*sides)]
        w,h=envelope(curves)
        result={"width":w,"depth":h,"wallThickness":0,"cornerRadius":corner_radius,"contours":curves}
    result["manufacturingRoute"]="Parametric"
    result.update({"kind":"polygon-bar","_familyParameters":dict(p),"radius":radius,
        "sideCount":sides,"specification":f"正{sides}边形棒，外接圆半径 {radius:g} mm，圆角 R {corner_radius:g} mm",
        "geometrySource":"providedBoundary" if actual else "idealizedFallback",
        "sourceRevision":p.get("sourceRevision","")})
    return result
