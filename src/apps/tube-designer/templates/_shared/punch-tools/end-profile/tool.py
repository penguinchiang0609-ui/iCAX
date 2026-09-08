"""Self-contained end-local extrusion cutter from the saved library/DXF section.

Local +X points into the selected end. Concave cuts remove the filled section;
convex cuts remove its complement only on the near side of the section, leaving
the far-side mother stock untouched. Neither mode cuts with a hollow pipe wall.
"""
import copy
import math


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
    """Extrema of a single clamped, degree 1..3 rational Bezier span.

    Imported DXF ellipses use four clamped rational quadratic spans. Preserve
    those exact curves and solve their projected derivative, not a polyline.
    Multi-span/periodic and higher-degree splines need a separate exact solver.
    """
    unsupported = "此样条暂不支持凸口自动定位；支持单段夹持的一至三次 B 样条 / NURBS（含 DXF 椭圆）"
    degree, points = segment.get("degree"), segment.get("controlPoints")
    if degree not in (1, 2, 3) or not isinstance(points, list) or len(points) != degree+1 or segment.get("periodic", False):
        raise ValueError(unsupported)
    if any(not isinstance(p, (list, tuple)) or len(p) != 2 or
           any(not isinstance(x, (int, float)) or not math.isfinite(x) for x in p) for p in points):
        raise ValueError("凸口样条控制点必须是有效二维坐标")
    knots = segment.get("knots", [])
    if not isinstance(knots, list) or not knots or any(not isinstance(x, (int, float)) or not math.isfinite(x) for x in knots):
        raise ValueError(unsupported)
    if "multiplicities" in segment:
        valid_knots = len(knots) == 2 and segment["multiplicities"] == [degree+1, degree+1]
    else:
        valid_knots = len(knots) == 2*(degree+1) and all(x == knots[0] for x in knots[:degree+1]) and all(x == knots[-1] for x in knots[degree+1:])
    if not valid_knots or knots[-1] <= knots[0]:
        raise ValueError(unsupported)
    if ("startParameter" in segment) != ("endParameter" in segment):
        raise ValueError("凸口样条须同时提供起止参数")
    start, end = segment.get("startParameter", knots[0]), segment.get("endParameter", knots[-1])
    if not all(isinstance(x, (int, float)) and math.isfinite(x) for x in (start, end)) or not knots[0] <= start < end <= knots[-1]:
        raise ValueError("凸口样条参数范围必须在当前夹持段内递增")
    start, end = (start-knots[0])/(knots[-1]-knots[0]), (end-knots[0])/(knots[-1]-knots[0])
    weights = segment.get("weights") if segment["kind"] == "nurbs" else [1.0]*(degree+1)
    if not isinstance(weights, list) or len(weights) != len(points) or any(not isinstance(w, (int, float)) or not math.isfinite(w) or w <= 0 for w in weights):
        raise ValueError("凸口有理样条需要与控制点数量一致的正有限权重")
    if segment["kind"] == "bspline" and "weights" in segment:
        raise ValueError("带权样条须使用 NURBS 类型")
    weights = [w/max(weights) for w in weights]
    # Translation has no effect on derivative roots. Subtract in coordinates
    # first, avoiding cancellation between large absolute N'W and NW' terms.
    origin = sum(points[0][i]*direction[i] for i in range(2))
    values = [sum((p[i]-points[0][i])*direction[i] for i in range(2)) for p in points]

    def power(bernstein):
        result = [0.0]*(degree+1)
        for i, value in enumerate(bernstein):
            for k in range(i, degree+1):
                result[k] += value*math.comb(degree, i)*math.comb(degree-i, k-i)*(-1)**(k-i)
        return result

    def derivative(p):
        return [i*p[i] for i in range(1, len(p))]

    def multiply(a, b):
        result = [0.0]*(len(a)+len(b)-1)
        for i, x in enumerate(a):
            for j, y in enumerate(b):
                result[i+j] += x*y
        return result

    def evaluate(p, t):
        value = 0.0
        for coefficient in reversed(p):
            value = value*t+coefficient
        return value

    def roots(p, lo, hi):
        scale = max((abs(x) for x in p), default=0)
        if scale == 0:
            return []
        p = [x/scale for x in p]
        while len(p) > 1 and abs(p[-1]) < 2e-14:
            p.pop()
        if len(p) <= 1:
            return []
        if len(p) == 2:
            root = -p[0]/p[1]
            return [root] if lo <= root <= hi else []
        # Derivative roots partition the polynomial into monotone intervals;
        # bisection isolates every sign-changing root without fixed sampling.
        partitions = [lo]+roots(derivative(p), lo, hi)+[hi]
        result = [t for t in partitions if abs(evaluate(p, t)) <= 2e-12]
        for left, right in zip(partitions, partitions[1:]):
            left_value, right_value = evaluate(p, left), evaluate(p, right)
            if left_value*right_value >= 0:
                continue
            for _ in range(64):
                middle = (left+right)/2
                value = evaluate(p, middle)
                if left_value*value <= 0:
                    right = middle
                else:
                    left, left_value = middle, value
            result.append((left+right)/2)
        return sorted(set(result))

    numerator = power([v*w for v, w in zip(values, weights)])
    denominator = power(weights)
    a, b = multiply(derivative(numerator), denominator), multiply(numerator, derivative(denominator))
    stationary = roots([x-y for x, y in zip(a, b)], start, end)

    def projected(t):
        # Positive-weight homogeneous de Casteljau avoids denominator
        # cancellation near a clamped endpoint during final evaluation.
        pairs = [[v*w, w] for v, w in zip(values, weights)]
        while len(pairs) > 1:
            pairs = [[(1-t)*a[k]+t*b[k] for k in range(2)] for a, b in zip(pairs, pairs[1:])]
        return origin+pairs[0][0]/pairs[0][1]

    extrema = [projected(t) for t in [start, end]+stationary]
    return min(extrema), max(extrema)


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
            elif sk in ("bspline", "nurbs"):
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
    return -extent, extent


def generate(p, context):
    contours = context.get("section",{}).get("profile",{}).get("contours")
    if not isinstance(contours,list) or not contours:
        raise ValueError("端切刀具需要管型库截面或本地 DXF 闭合截面")
    # The profile protocol puts its outer boundary first. Inner pipe walls and
    # imported inner loops never turn an end-forming cutter into a ring.
    contours = copy.deepcopy(contours[:1])
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    place = context["placement"]
    if place["datum"] != "long":
        raise ValueError("截面端切刀具使用原端长点定位，不支持猜测曲面中心或短点")
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"] >= hi[0]-lo[0]:
        raise ValueError("端部修剪量须小于母材长度")
    a,b,r = map(math.radians,(p["angle"],p["azimuth"],p["roll"]))
    sa,ca,sb,cb = math.sin(a),math.cos(a),math.sin(b),math.cos(b)
    axis = [ca,sa*sb,sa*cb]
    normal, across = [sa,-ca*sb,-ca*cb],[0,cb,-sb]
    u,v = normal,across
    u,v = ([u[i]*math.cos(r)+v[i]*math.sin(r) for i in range(3)],
        [-u[i]*math.sin(r)+v[i]*math.cos(r) for i in range(3)])
    center = [p["axialOffset"],p["offsetY"],p["offsetZ"]]
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
            "contours":[{"kind":"roundedRectangle","width":reach,"height":2*reach,"radius":0}]}},
        {"key":"slab","operator":"extrude","inputs":["slab-section"],"arguments":{"vector":[0,0,2*reach]}}
    ]
    if mode == "convex":
        # E's cutoff follows the section's middle plane and is parallel to its
        # extrusion axis. Only the near half is shaped; the far stock remains.
        nodes.extend([
            {"key":"forming-section","operator":"profile2d","arguments":{"placement":{
                "origin":[center[i]+normal[i]*middle for i in range(3)],"xAxis":axis,"yAxis":across},
                "contours":[{"kind":"roundedRectangle","width":2*reach,"height":2*reach,"radius":0}]}},
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
