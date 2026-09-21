"""Self-contained end-local extrusion cutter from the saved library/DXF section.

Local +X points into the selected end. Concave cuts remove the filled section;
convex cuts remove its complement only on the near side of the section, leaving
the far-side mother stock untouched. Neither mode cuts with a hollow pipe wall.
"""
import copy
import math


def _rect_path(width, height):
    points=[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]]
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line","start":points[i],"end":points[(i+1)%4]} for i in range(4)
    ]}


def _arc_support(center, a, b, start, end, direction):
    """Exact projection extrema of center + a*cos(t) + b*sin(t)."""
    c = sum(center[i]*direction[i] for i in range(2))
    x = sum(a[i]*direction[i] for i in range(2))
    y = sum(b[i]*direction[i] for i in range(2))
    angles = [start, end]
    stationary = math.atan2(y, x)
    for angle in (stationary, stationary+math.pi):
        angle += math.ceil((start-angle)/math.tau)*math.tau
        if angle <= end+1e-12:
            angles.append(angle)
    values = [c+x*math.cos(angle)+y*math.sin(angle) for angle in angles]
    return min(values), max(values)


def _spline_support(segment, direction):
    # The host injects the same geometry queries for every mould. Standalone
    # execution uses that implementation too, rather than a second solver.
    queries = globals().get("section_geometry")
    if queries is None:
        import importlib.util
        from pathlib import Path
        path = Path(__file__).resolve().parents[2] / "_shared" / "section_geometry.py"
        spec = importlib.util.spec_from_file_location("icax_section_queries", path)
        queries = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(queries)
    return queries.spline_support(segment, direction)


def _support(contour, direction):
    """Analytic outer-contour envelope, never a sampled replacement contour."""
    kind = contour["kind"]
    dx, dy = direction
    if kind == "polygon":
        values = [point[0]*dx+point[1]*dy for point in contour["points"]]
        return min(values), max(values)
    if kind == "circle":
        extent = contour["radius"]*math.hypot(dx,dy)
    elif kind == "ellipse":
        extent = math.hypot(dx*contour["width"]/2,dy*contour["height"]/2)
    elif kind in ("roundedRectangle", "capsule"):
        hx, hy = contour["width"]/2, contour["height"]/2
        radius = min(hx,hy) if kind == "capsule" else contour.get("radius",0)
        extent = abs(dx)*(hx-radius)+abs(dy)*(hy-radius)+radius*math.hypot(dx,dy)
    elif kind == "path":
        ranges = []
        for segment in contour["segments"]:
            sk = segment["kind"]
            if sk == "line":
                values = [segment[key][0]*dx+segment[key][1]*dy for key in ("start","end")]
                ranges.append((min(values),max(values)))
            elif sk == "ellipseArc":
                rotation = segment.get("rotation",0)
                cr, sr = math.cos(rotation), math.sin(rotation)
                ranges.append(_arc_support(segment["center"],
                    [segment["majorRadius"]*cr,segment["majorRadius"]*sr],
                    [-segment["minorRadius"]*sr,segment["minorRadius"]*cr],
                    segment["startAngle"],segment["endAngle"],direction))
            elif sk in ("bezier", "bspline", "nurbs"):
                ranges.append(_spline_support(segment, direction))
            elif sk == "arc":
                p0, pm, p1 = (segment[key] for key in ("start","middle","end"))
                bx, by, cx, cy = pm[0]-p0[0],pm[1]-p0[1],p1[0]-p0[0],p1[1]-p0[1]
                denominator = 2*(bx*cy-by*cx)
                if abs(denominator) < 1e-12:
                    raise ValueError("凸口轮廓包含退化圆弧")
                center = [p0[0]+(cy*(bx*bx+by*by)-by*(cx*cx+cy*cy))/denominator,
                          p0[1]+(bx*(cx*cx+cy*cy)-cx*(bx*bx+by*by))/denominator]
                radius = math.hypot(p0[0]-center[0],p0[1]-center[1])
                t0, tm, t1 = [math.atan2(point[1]-center[1],point[0]-center[0]) for point in (p0,pm,p1)]
                if (tm-t0)%math.tau <= (t1-t0)%math.tau:
                    start, end = t0, t0+(t1-t0)%math.tau
                else:
                    start, end = t1, t1+(t0-t1)%math.tau
                ranges.append(_arc_support(center,[radius,0],[0,radius],start,end,direction))
            else:
                raise ValueError("此曲线暂不支持凸口自动定位，请使用线段、圆弧或椭圆弧组成的闭合截面")
        if not ranges:
            raise ValueError("凸口需要非空闭合截面")
        return min(value[0] for value in ranges),max(value[1] for value in ranges)
    else:
        raise ValueError("此截面暂不支持凸口自动定位")
    center = contour.get("center", [0, 0])
    shift = center[0]*dx + center[1]*dy
    return shift-extent, shift+extent


def generate(p, context):
    contours = context.get("section",{}).get("profile",{}).get("contours")
    if not isinstance(contours,list) or not contours:
        raise ValueError("端切刀具需要管型库截面或本地 DXF 闭合截面")
    # The profile protocol puts its outer boundary first. Inner pipe walls and
    # imported inner loops never turn an end-forming cutter into a ring.
    contours = copy.deepcopy(contours[:1])
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    place = context["placement"]
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"] >= hi[0]-lo[0]:
        raise ValueError("端部修剪量须小于母材长度")
    a,b,r = map(math.radians,(place.get("angle",90),place.get("azimuth",0),place.get("roll",0)))
    sa,ca,sb,cb = math.sin(a),math.cos(a),math.sin(b),math.cos(b)
    axis = [ca,sa*sb,sa*cb]
    normal, across = [sa,-ca*sb,-ca*cb],[0,cb,-sb]
    u,v = normal,across
    u,v = ([u[i]*math.cos(r)+v[i]*math.sin(r) for i in range(3)],
        [-u[i]*math.sin(r)+v[i]*math.cos(r) for i in range(3)])
    center = [place.get("axialOffset",0),place.get("offsetY",0),place.get("offsetZ",0)]
    mode = p.get("cutMode","concave")
    envelope = 0
    if mode == "convex":
        if abs(sa) < 1e-6:
            raise ValueError("凸口的支管轴不能与主管轴平行，请调整轴夹角")
        low, high = _support(contours[0],(math.cos(r),-math.sin(r)))
        center[0] -= low/sa
        middle = (low+high)/2
        envelope = max(abs(value) for d in ((1,0),(0,1)) for value in _support(contours[0],d))
    elif mode != "concave":
        raise ValueError("端部成形方式无效")
    # Include the derived center/support in the finite construction envelope;
    # shallow oblique poses can put the cylinder axis far beyond the stock end.
    reach = 4*(sum(hi[i]-lo[i] for i in range(3))+sum(abs(value) for value in center)+envelope+10)
    nodes = [
        {"key":"section","operator":"profile2d","arguments":{"placement":{
            "origin":[center[i]-axis[i]*reach for i in range(3)],"xAxis":u,"yAxis":v},"contours":contours}},
        {"key":"section-tool","operator":"extrude","inputs":["section"],"arguments":{"vector":[n*2*reach for n in axis]}},
        {"key":"slab-section","operator":"profile2d","arguments":{"placement":{
            "origin":[-reach/2,0,-reach],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "contours":[_rect_path(reach,2*reach)]}},
        {"key":"slab","operator":"extrude","inputs":["slab-section"],"arguments":{"vector":[0,0,2*reach]}}
    ]
    if mode == "convex":
        # E's cutoff follows the section's middle plane and is parallel to its
        # extrusion axis. Only the near half is shaped; the far stock remains.
        nodes.extend([
            {"key":"forming-section","operator":"profile2d","arguments":{"placement":{
                "origin":[center[i]+normal[i]*middle for i in range(3)],"xAxis":axis,"yAxis":across},
                "contours":[_rect_path(2*reach,2*reach)]}},
            {"key":"forming-region","operator":"extrude","inputs":["forming-section"],
                "arguments":{"vector":[-2*reach*value for value in normal]}},
            {"key":"convex-cut","operator":"boolean","inputs":["forming-region","section-tool"],
                "arguments":{"operation":"subtract"}},
            {"key":"tool","operator":"boolean","inputs":["slab","convex-cut"],"arguments":{"operation":"union"}}
        ])
    else:
        nodes.append({"key":"tool","operator":"boolean","inputs":["slab","section-tool"],"arguments":{"operation":"union"}})
    return {"mode":"solid","coordinateSpace":"end-local","outputKey":"tool","datumCenter":None,"requireEndContact":True,
        "model":{"schema":"icax.neutral-model","schemaVersion":1,
            "template":{"id":"end-profile","version":"1.0.0","packageDigest":"self-contained"},"geometry":nodes}}
