"""Sigma shortcut template built as one continuous equal-thickness strip."""
import math


def _finite(value, name):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError(f"{name}必须为有限数值")
    return value


def _positive(value, name):
    value = _finite(value, name)
    if value <= 1e-6:
        raise ValueError(f"{name}必须大于零")
    return value


def _polygon(points, radii):
    points = [list(point) for point in points]
    if len(points) < 3 or len(points) != len(radii):
        raise ValueError("Sigma 偏置轮廓定义无效")
    corners = []
    for index, vertex in enumerate(points):
        before, after = points[index - 1], points[(index + 1) % len(points)]
        incoming = [vertex[i] - before[i] for i in (0, 1)]
        outgoing = [after[i] - vertex[i] for i in (0, 1)]
        incoming_length, outgoing_length = math.hypot(*incoming), math.hypot(*outgoing)
        if min(incoming_length, outgoing_length) <= 1e-8:
            raise ValueError("Sigma 偏置轮廓存在零长度边")
        incoming = [value / incoming_length for value in incoming]
        outgoing = [value / outgoing_length for value in outgoing]
        turn = math.atan2(incoming[0] * outgoing[1] - incoming[1] * outgoing[0],
                          sum(a * b for a, b in zip(incoming, outgoing)))
        radius = _finite(radii[index], "折弯内 R")
        if radius < 0:
            raise ValueError("折弯内 R 不能为负数")
        tangent = radius * math.tan(abs(turn) / 2)
        if tangent >= min(incoming_length, outgoing_length) - 1e-8:
            raise ValueError("圆角超过相邻直边长度")
        start = [vertex[i] - incoming[i] * tangent for i in (0, 1)]
        end = [vertex[i] + outgoing[i] * tangent for i in (0, 1)]
        sign = 1 if turn > 0 else -1
        center = [start[0] - incoming[1] * sign * radius,
                  start[1] + incoming[0] * sign * radius]
        angle = math.atan2(start[1] - center[1], start[0] - center[0]) + turn / 2
        middle = [center[0] + radius * math.cos(angle),
                  center[1] + radius * math.sin(angle)]
        corners.append((start, end, middle, tangent, radius))
    segments = []
    for index, (start, end, middle, tangent, radius) in enumerate(corners):
        previous = corners[index - 1]
        if previous[3] + tangent >= math.dist(points[index - 1], points[index]) - 1e-8:
            raise ValueError("相邻圆角重叠")
        segments.append({"kind": "line", "start": previous[1], "end": start})
        if radius > 0:
            segments.append({"kind": "arc", "start": start, "middle": middle, "end": end})
    return {"kind": "path", "closed": True, "segments": segments}


def _simplify(points, bend_radii):
    points = [list(point) for point in points]
    bend_radii = list(bend_radii)
    changed = True
    while changed:
        changed = False
        for index in range(1, len(points) - 1):
            a, b, c = points[index - 1], points[index], points[index + 1]
            u = [b[i] - a[i] for i in (0, 1)]
            v = [c[i] - b[i] for i in (0, 1)]
            lu, lv = math.hypot(*u), math.hypot(*v)
            if min(lu, lv) <= 1e-8:
                raise ValueError("中心路径存在零长度板段")
            cross = u[0] * v[1] - u[1] * v[0]
            dot = u[0] * v[0] + u[1] * v[1]
            if abs(cross) <= 1e-10 * lu * lv and dot > 0:
                del points[index]
                del bend_radii[index - 1]
                changed = True
                break
    return points, bend_radii


def _strip(points, thickness, inner_radii):
    directions = []
    for start, end in zip(points, points[1:]):
        vector = [end[i] - start[i] for i in (0, 1)]
        length = math.hypot(*vector)
        if length <= 1e-8:
            raise ValueError("中心路径存在零长度板段")
        directions.append([value / length for value in vector])
    if len(inner_radii) != len(points) - 2:
        raise ValueError("折弯半径数量与中心路径不一致")
    sides, side_radii = [], []
    for sign in (1, -1):
        vertices, radii = [], []
        for index, point in enumerate(points):
            incoming = directions[max(0, index - 1)]
            outgoing = directions[min(index, len(directions) - 1)]
            if index in (0, len(points) - 1):
                vertices.append([point[0] - sign * outgoing[1] * thickness / 2,
                                 point[1] + sign * outgoing[0] * thickness / 2])
                radii.append(0)
                continue
            cross = incoming[0] * outgoing[1] - incoming[1] * outgoing[0]
            dot = incoming[0] * outgoing[0] + incoming[1] * outgoing[1]
            if abs(cross) <= 1e-10 or 1 + dot <= 1e-10:
                raise ValueError("中心路径相邻板段方向无效")
            shift = sign * thickness / 2 / (1 + dot)
            vertices.append([point[0] - shift * (incoming[1] + outgoing[1]),
                             point[1] + shift * (incoming[0] + outgoing[0])])
            inner = inner_radii[index - 1]
            radii.append(inner if sign * cross > 0 else inner + thickness)
        sides.append(vertices)
        side_radii.append(radii)
    return _polygon(sides[0] + list(reversed(sides[1])),
                    side_radii[0] + list(reversed(side_radii[1])))


def _arc_center(edge):
    a, m, b = edge["start"], edge["middle"], edge["end"]
    bx, by = m[0] - a[0], m[1] - a[1]
    cx, cy = b[0] - a[0], b[1] - a[1]
    denominator = 2 * (bx * cy - by * cx)
    if abs(denominator) <= 1e-12:
        raise ValueError("圆弧定义退化")
    return [a[0] + (cy * (bx * bx + by * by) - by * (cx * cx + cy * cy)) / denominator,
            a[1] + (bx * (cx * cx + cy * cy) - cx * (bx * bx + by * by)) / denominator]


def _envelope(contour):
    points = []
    for edge in contour["segments"]:
        points.extend((edge["start"], edge["end"]))
        if edge["kind"] != "arc":
            continue
        center = _arc_center(edge)
        radius = math.dist(edge["start"], center)
        start, middle, end = [math.atan2(point[1] - center[1], point[0] - center[0])
                              for point in (edge["start"], edge["middle"], edge["end"])]
        ccw = (middle - start) % math.tau <= (end - start) % math.tau
        for angle in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
            inside = ((angle - start) % math.tau <= (end - start) % math.tau + 1e-10
                      if ccw else
                      (start - angle) % math.tau <= (start - end) % math.tau + 1e-10)
            if inside:
                points.append([center[0] + radius * math.cos(angle),
                               center[1] + radius * math.sin(angle)])
    low = [min(point[i] for point in points) for i in (0, 1)]
    high = [max(point[i] for point in points) for i in (0, 1)]
    return high[0] - low[0], high[1] - low[1]


def _side_values(p):
    if p.get("useIndependentSides", False):
        return (float(p["topFlangeWidth"]), float(p["bottomFlangeWidth"]),
                float(p["topLipLength"]), float(p["bottomLipLength"]),
                float(p["topOuterWebHeight"]), float(p["bottomOuterWebHeight"]),
                float(p["upperTransitionRise"]), float(p["lowerTransitionRise"]),
                float(p["topLipAngle"]), float(p["bottomLipAngle"]))
    return (float(p["flangeWidth"]), float(p["flangeWidth"]),
            float(p["lipLength"]), float(p["lipLength"]),
            float(p["outerWebHeight"]), float(p["outerWebHeight"]),
            float(p["transitionRise"]), float(p["transitionRise"]),
            float(p["lipAngle"]), float(p["lipAngle"]))


def _radii(p):
    values = ([float(p[f"bendRadius{index}"]) for index in range(1, 9)]
              if p.get("useIndependentRadii", False)
              else [float(p["bendRadius"])] * 8)
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("折弯内 R 必须为非负有限数值")
    return values


def _centreline(p):
    height = _positive(p["depth"], "总高度 H")
    thickness = _positive(p["wallThickness"], "板厚 t")
    if height <= thickness:
        raise ValueError("总高度 H 必须大于板厚 t")
    top_b, bottom_b, top_c, bottom_c, top_a, bottom_a, top_q, bottom_q, top_theta, bottom_theta = _side_values(p)
    for value, name in ((top_b, "上翼缘 Bt"), (bottom_b, "下翼缘 Bb"),
                        (top_a, "上外腹板 at"), (bottom_a, "下外腹板 ab"),
                        (top_q, "上过渡 qt"), (bottom_q, "下过渡 qb")):
        _positive(value, name)
    for value, name in ((top_c, "上卷边 Ct"), (bottom_c, "下卷边 Cb")):
        if not math.isfinite(value) or value < 0:
            raise ValueError(f"{name}必须为非负有限数值")
    for value, name in ((top_theta, "上卷边角"), (bottom_theta, "下卷边角")):
        if not math.isfinite(value) or not 0 < value < 180:
            raise ValueError(f"{name}必须大于 0° 且小于 180°")
    offset = _finite(p["centerWebOffset"], "中央腹板偏移 e")
    middle_height = height - thickness - top_a - bottom_a - top_q - bottom_q
    if middle_height <= 1e-6:
        raise ValueError("中央腹板高度必须大于零")

    top_y, bottom_y = (height - thickness) / 2, -(height - thickness) / 2
    radii = _radii(p)
    points, bends = [], []
    top_corner = [top_b, top_y]
    if top_c > 1e-8:
        angle = math.radians(top_theta)
        points.extend(([top_b - top_c * math.cos(angle), top_y - top_c * math.sin(angle)], top_corner))
        bends.append(radii[0])
    else:
        points.append(top_corner)
    points.append([0, top_y]); bends.append(radii[1])
    points.append([0, top_y - top_a]); bends.append(radii[2])
    points.append([offset, top_y - top_a - top_q]); bends.append(radii[3])
    points.append([offset, bottom_y + bottom_a + bottom_q]); bends.append(radii[4])
    points.append([0, bottom_y + bottom_a]); bends.append(radii[5])
    points.append([0, bottom_y]); bends.append(radii[6])
    bottom_corner = [bottom_b, bottom_y]
    points.append(bottom_corner)
    if bottom_c > 1e-8:
        angle = math.radians(bottom_theta)
        points.append([bottom_b - bottom_c * math.cos(angle), bottom_y + bottom_c * math.sin(angle)])
        bends.append(radii[7])
    points, bends = _simplify(points, bends)
    if p.get("mirrorX", False):
        points = [[-point[0], point[1]] for point in points]
    if p.get("mirrorY", False):
        points = [[point[0], -point[1]] for point in points]
    return points, bends, thickness


def build(parameters):
    p = dict(parameters)
    points, radii, thickness = _centreline(p)
    contour = _strip(points, thickness, radii)
    width, depth = _envelope(contour)
    if min(width, depth) <= 1e-6:
        raise ValueError("Sigma 截面包络无效")
    return {
        "contours": [contour],
        **p,
        "_parameters": p,
        "kind": "sigma",
        "width": width,
        "depth": depth,
        "wallThickness": thickness,
        "cornerRadius": float(p["bendRadius"]),
        "specification": f"Sigma 型钢 {depth:g} × {width:g} mm",
        "manufacturingRoute": "ColdFormedSigma",
        "geometrySource": "idealizedParametric",
    }
