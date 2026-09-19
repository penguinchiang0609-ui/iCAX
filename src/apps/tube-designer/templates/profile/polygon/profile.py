"""通用正多边形管截面；外轮廓和内轮廓均以二维 path 表达。"""
import math


def _positive(*values):
    if any(not math.isfinite(value) or value <= 1e-9 for value in values):
        raise ValueError("尺寸必须为大于零的有限数值")


def _rounded_path(points, radii):
    corners = []
    count = len(points)
    if count < 3 or len(radii) != count:
        raise ValueError("轮廓顶点与圆角数量不一致")
    for index, point in enumerate(points):
        previous = points[index - 1]
        following = points[(index + 1) % count]
        incoming = [point[i] - previous[i] for i in (0, 1)]
        outgoing = [following[i] - point[i] for i in (0, 1)]
        incoming_length = math.hypot(*incoming)
        outgoing_length = math.hypot(*outgoing)
        _positive(incoming_length, outgoing_length)
        incoming = [value / incoming_length for value in incoming]
        outgoing = [value / outgoing_length for value in outgoing]
        turn = math.atan2(incoming[0] * outgoing[1] - incoming[1] * outgoing[0],
                          sum(incoming[i] * outgoing[i] for i in (0, 1)))
        radius = float(radii[index])
        if not math.isfinite(radius) or radius < 0:
            raise ValueError("圆角半径必须为非负有限数值")
        tangent = radius * math.tan(abs(turn) / 2)
        if tangent >= min(incoming_length, outgoing_length) - 1e-9:
            raise ValueError("圆角超过相邻边长度")
        start = [point[i] - incoming[i] * tangent for i in (0, 1)]
        end = [point[i] + outgoing[i] * tangent for i in (0, 1)]
        middle = None
        if radius:
            sign = 1 if turn > 0 else -1
            center = [start[0] - incoming[1] * sign * radius,
                      start[1] + incoming[0] * sign * radius]
            angle = math.atan2(start[1] - center[1], start[0] - center[0]) + turn / 2
            middle = [center[0] + radius * math.cos(angle),
                      center[1] + radius * math.sin(angle)]
        corners.append((start, end, tangent, radius, middle))
    segments = []
    for index, current in enumerate(corners):
        previous = corners[index - 1]
        if previous[2] + current[2] >= math.dist(points[index - 1], points[index]) - 1e-9:
            raise ValueError("相邻圆角重叠")
        segments.append({"kind": "line", "start": previous[1], "end": current[0]})
        if current[3]:
            segments.append({"kind": "arc", "start": current[0],
                             "middle": current[4], "end": current[1]})
    return {"kind": "path", "closed": True, "segments": segments}


def _parse_radii(value, count, default):
    if isinstance(value, str):
        text = value.replace("，", ",").strip()
        values = [float(item.strip()) for item in text.split(",")] if text else [default] * count
    else:
        values = list(value)
    if len(values) != count or any(not math.isfinite(float(item)) or float(item) < 0 for item in values):
        raise ValueError(f"圆角须填写 {count} 个非负数，按顶点顺序用逗号分隔")
    return [float(item) for item in values]


def _regular_points(sides, circumradius, center_x=0.0, center_y=0.0):
    phase = math.pi / 2 - math.pi / sides
    return [[center_x + circumradius * math.cos(phase + math.tau * index / sides),
             center_y + circumradius * math.sin(phase + math.tau * index / sides)]
            for index in range(sides)]


def _shape(parameters):
    sides = int(parameters["sideCount"])
    circumradius = float(parameters["radius"])
    wall = float(parameters["wallThickness"])
    outer_radius = float(parameters["outerRadius"])
    offset_x = float(parameters["innerOffsetX"])
    offset_y = float(parameters["innerOffsetY"])
    if sides < 3 or sides > 32:
        raise ValueError("边数必须在 3 到 32 之间")
    _positive(circumradius, wall)
    if not math.isfinite(outer_radius) or outer_radius < 0:
        raise ValueError("外 R 必须为非负有限数值")
    apothem = circumradius * math.cos(math.pi / sides)
    inner_apothem = apothem - wall
    if inner_apothem <= 1e-9:
        raise ValueError("壁厚必须小于外接圆内切半径")
    if abs(offset_x) >= wall or abs(offset_y) >= wall:
        raise ValueError("内轮廓偏心不能达到壁厚")

    if parameters["useIndependentOuterRadii"]:
        outer_radii = _parse_radii(parameters["outerRadii"], sides, outer_radius)
    else:
        outer_radii = [outer_radius] * sides
    inner_circumradius = inner_apothem / math.cos(math.pi / sides)
    if parameters["useIndependentInnerRadii"]:
        inner_radii = _parse_radii(parameters["innerRadii"], sides, 0.0)
    else:
        inner_radii = [max(value - wall, 0.0) for value in outer_radii]
    outer_points = _regular_points(sides, circumradius)
    inner_points = _regular_points(sides, inner_circumradius, offset_x, offset_y)
    return [_rounded_path(outer_points, outer_radii),
            _rounded_path(inner_points, inner_radii)]


def build(parameters):
    parameters = dict(parameters)
    contours = _shape(parameters)
    sides = int(parameters["sideCount"])
    radius = float(parameters["radius"])
    return {
        "contours": contours,
        **parameters,
        "kind": "polygon",
        "width": 2 * radius,
        "depth": 2 * radius,
        "wallThickness": float(parameters["wallThickness"]),
        "cornerRadius": float(parameters["outerRadius"]),
        "specification": f"正{sides}边形管，外接圆半径 {radius:g} mm",
        "manufacturingRoute": "Parametric",
    }
