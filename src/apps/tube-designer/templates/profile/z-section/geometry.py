"""Generic exact section construction, with no market-name dispatch.

Bundled as geometry.py in each profile package so exported packages are complete.
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
    if not any(rs):return {"kind":"polygon","points":points}
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
    from .section_geometry import spline_support
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
