"""Type-agnostic exact 2D queries for mould scripts. No profile fitting or IDs."""
import copy
import math


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


def from_profile(profile, tolerance=0.001):
    """Read exact generic contours when BRep projection cannot extract a loop.

    Reuse the geometry-only line/arc normalizer; no fitting, profile IDs,
    nominal dimensions or forward generation participate in this conversion.
    The blank builder supplies outer contour first, followed by inner contours.
    Unsupported curves remain unsupported, never approximated with a polygon.
    """
    import importlib.util
    from pathlib import Path
    spec = importlib.util.spec_from_file_location(
        "section_boundary_geometry", Path(__file__).with_name("profile_recognition_geometry.py"))
    geometry = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(geometry)
    tolerance = number(tolerance)
    raw = profile.get("contours") if isinstance(profile, dict) else None
    if tolerance <= 0 or not isinstance(raw, list) or not 1 <= len(raw) <= 1000:
        raise SectionError("缺少有效通用截面轮廓")
    if any(c.get("closed") is False for c in raw):
        raise SectionError("槽口要求闭合截面轮廓")
    try:
        loops = [geometry.contour(c, tolerance) for c in raw]
    except (ValueError, KeyError, TypeError) as error:
        raise SectionError("无法解析通用截面精确边界：" + str(error)) from error
    box = geometry.bounds(loops[0])
    center = [(box[0]+box[2])/2, (box[1]+box[3])/2]
    contours = []
    def local(p):
        return [v-c for v,c in zip(point(p), center)]
    for index, loop in enumerate(loops):
        if loop["kind"] in ("circle", "ellipse"):
            angle = number(loop.get("angle", 0))
            c, s = math.cos(angle), math.sin(angle)
            edge = {"kind": "circleArc" if loop["kind"] == "circle" else "ellipseArc",
                    "center": local(loop["center"]), "xAxis": [c,s], "yAxis": [-s,c],
                    "first": 0.0, "last": math.tau}
            if loop["kind"] == "circle": edge["radius"] = number(loop["radius"])
            else: edge.update(majorRadius=number(loop["a"]), minorRadius=number(loop["b"]))
            edge["start"] = edge["end"] = arc_point(edge, 0)
            edges = [edge]
        else:
            edges = []
            for original in loop["edges"]:
                edge = {"kind": original["kind"], "start": local(original["start"]), "end": local(original["end"])}
                if original["kind"] == "arc":
                    first = math.atan2(original["start"][1]-original["center"][1], original["start"][0]-original["center"][0])
                    edge.update(kind="circleArc", center=local(original["center"]), radius=number(original["radius"]),
                                xAxis=[1,0], yAxis=[0,1], first=first, last=first+original["sweep"])
                edges.append(edge)
        contours.append({"inner": index > 0, "closed": True, "edges": edges})
    # Validate connectivity/finite coordinates through the shared query entry.
    return local_section({"schema": "icax.mold-section", "schemaVersion": 1,
                          "status": "available", "tolerance": tolerance, "contours": contours})


def local_section(section, rotation=0):
    """Express the unmodified section in the cutter frame (inverse X rotation)."""
    if not isinstance(section, dict) or section.get("schema") != "icax.mold-section" or section.get("schemaVersion") != 1:
        raise SectionError("缺少精确主管截面")
    if section.get("status") != "available":
        raise SectionError(section.get("reason", "主管截面不可用"))
    result = copy.deepcopy(section)
    tolerance = number(result.get("tolerance", 0.001))
    if tolerance <= 0:
        raise SectionError("截面容差无效")
    loops = result.get("contours")
    if not isinstance(loops, list) or not 1 <= len(loops) <= 1000:
        raise SectionError("主管截面轮廓数量无效")
    angle = -math.radians(number(rotation))
    c, s = math.cos(angle), math.sin(angle)

    def rotate(p):
        x, y = point(p)
        return [c*x-s*y, s*x+c*y]

    for loop in loops:
        if not isinstance(loop.get("inner"), bool) or not isinstance(loop.get("closed"), bool):
            raise SectionError("截面缺少内外环或闭合标记")
        edges = loop.get("edges")
        if not isinstance(edges, list) or not 1 <= len(edges) <= 10000:
            raise SectionError("截面边数量无效")
        for edge in edges:
            for key in ("start", "end", "center", "xAxis", "yAxis"):
                if key in edge:
                    edge[key] = rotate(edge[key])
            for key in ("poles", "points"):
                if key in edge:
                    edge[key] = [rotate(p) for p in edge[key]]
        if loop["closed"]:
            for index, edge in enumerate(edges):
                a, b = point(edge["end"]), point(edges[(index+1) % len(edges)]["start"])
                if math.dist(a, b) > tolerance:
                    raise SectionError("截面闭合标记与实际边连接不一致")
    return result


def arc_point(edge, t):
    center = point(edge["center"])
    x, y = point(edge["xAxis"]), point(edge["yAxis"])
    a = number(edge["radius"] if edge["kind"] == "circleArc" else edge["majorRadius"])
    b = a if edge["kind"] == "circleArc" else number(edge["minorRadius"])
    if min(a, b) <= 0:
        raise SectionError("截面圆弧半径无效")
    return [center[i]+a*math.cos(t)*x[i]+b*math.sin(t)*y[i] for i in (0,1)]


def in_arc_range(edge, t):
    first, last = number(edge["first"]), number(edge["last"])
    if abs(last-first) >= math.tau-1e-10:
        return True
    if "counterClockwise" in edge:
        span = (last-first) % math.tau if edge["counterClockwise"] else (first-last) % math.tau
        return ((t-first) % math.tau if edge["counterClockwise"] else (first-t) % math.tau) <= span+1e-10
    low, high = sorted((first,last))
    return high-low >= math.tau-1e-10 or (t-low) % math.tau <= high-low+1e-10


def bounds(contour):
    """Analytic arc extrema and tolerance-bounded exact spline envelopes."""
    points = []
    for edge in contour["edges"]:
        points.extend((point(edge["start"]), point(edge["end"])))
        if edge["kind"] == "line":
            continue
        if edge["kind"] == "polyline":
            points.extend(point(p) for p in edge["points"])
            continue
        if edge["kind"] in ("bezier", "bspline", "nurbs"):
            x = spline_support(edge,[1,0])
            y = spline_support(edge,[0,1])
            points.extend(([x[0],y[0]],[x[1],y[1]]))
            continue
        if edge["kind"] not in ("circleArc", "ellipseArc"):
            raise SectionError("此几何查询尚不支持该精确曲线，不能用采样近似判断槽口适用性")
        a = number(edge["radius"] if edge["kind"] == "circleArc" else edge["majorRadius"])
        b = a if edge["kind"] == "circleArc" else number(edge["minorRadius"])
        for i in (0,1):
            t = math.atan2(b*edge["yAxis"][i], a*edge["xAxis"][i])
            for candidate in (t,t+math.pi):
                if in_arc_range(edge,candidate):
                    points.append(arc_point(edge,candidate))
    return {"min":[min(p[i] for p in points) for i in (0,1)],
            "max":[max(p[i] for p in points) for i in (0,1)]}


def horizontal_support(contour, height, tolerance, across=0):
    """Find a real straight boundary at height covering the requested station."""
    matches=[]
    for edge in contour["edges"]:
        if edge["kind"] != "line":
            continue
        a,b=point(edge["start"]),point(edge["end"])
        if abs(a[1]-height)>tolerance or abs(b[1]-height)>tolerance:
            continue
        low, high=sorted((a[0],b[0]))
        if low+tolerance < across < high-tolerance:
            matches.append({"height":height,"range":[low,high],"start":a,"end":b})
    if len(matches)!=1:
        raise SectionError("当前加工方向的基准位置没有唯一的平直壁面")
    return matches[0]


def parallel_gap(first, second):
    """Distance and common span between two horizontal supports."""
    low=max(first["range"][0],second["range"][0])
    high=min(first["range"][1],second["range"][1])
    if low>=high:
        raise SectionError("内外壁没有共同的平直区域")
    return {"distance":abs(second["height"]-first["height"]),"range":[low,high]}


def closed_shell_metrics(section, rotation=0):
    """Measure one closed shell; callers decide which supports they require."""
    local = local_section(section, rotation)
    loops = local["contours"]
    outer = [c for c in loops if not c["inner"]]
    inner = [c for c in loops if c["inner"]]
    if len(outer) != 1 or len(inner) != 1 or not all(c["closed"] for c in loops):
        raise SectionError("需要一个闭合外轮廓和一个闭合内轮廓")
    tol = number(local.get("tolerance", 0.001))
    if tol <= 0:
        raise SectionError("截面容差必须为正数")
    outside, inside = bounds(outer[0]), bounds(inner[0])
    if any(not outside["min"][i] < inside["min"][i] < inside["max"][i] < outside["max"][i] for i in (0, 1)):
        raise SectionError("内轮廓必须严格位于外轮廓内部")
    data = {"outside": outside, "inside": inside, "contours": loops, "tolerance": tol}
    circles = []
    for loop in (outer[0], inner[0]):
        edges = loop["edges"]
        if not edges or any(e["kind"] != "circleArc" for e in edges):
            break
        center, radius = edges[0]["center"], edges[0]["radius"]
        if any(math.dist(e["center"], center) > tol or abs(e["radius"]-radius) > tol for e in edges):
            break
        if abs(sum(abs(e["last"]-e["first"]) for e in edges)-math.tau)*radius > tol:
            break
        circles.append((center, radius))
    if len(circles) == 2:
        wall = circles[0][1]-circles[1][1]
        if wall <= tol or math.dist(circles[0][0], circles[1][0]) > tol:
            raise SectionError("圆形内外壁必须同心且壁厚为正")
        return {**data, "circularShell": True, "wallThickness": wall}
    across = (inside["min"][0]+inside["max"][0])/2
    gaps = []
    for side in ("min", "max"):
        a = horizontal_support(outer[0], outside[side][1], tol, across)
        b = horizontal_support(inner[0], inside[side][1], tol, across)
        gaps.append(parallel_gap(a, b)["distance"])
    if min(gaps) <= tol or abs(gaps[0]-gaps[1]) > tol:
        raise SectionError("上下基准壁须为等厚的平直壁面")
    return {**data, "circularShell": False, "wallThickness": gaps[0]}
