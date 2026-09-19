"""通用单安装翼 P 型管截面。所有轮廓均以二维 path 表达。"""
import math


def _positive(*values):
    if any(not math.isfinite(value) or value <= 1e-9 for value in values):
        raise ValueError("尺寸必须为大于零的有限数值")


def _rounded_path(points, radii):
    if len(points) < 3 or len(points) != len(radii):
        raise ValueError("轮廓顶点与圆角数量不一致")
    corners = []
    for index, point in enumerate(points):
        previous = points[index - 1]
        following = points[(index + 1) % len(points)]
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


def _shape(parameters):
    width = float(parameters["width"])
    height = float(parameters["depth"])
    wall = float(parameters["wallThickness"])
    flange_length = float(parameters["flangeLength"])
    flange_thickness = float(parameters["flangeThickness"])
    flange_root_radius = float(parameters["flangeRootRadius"])
    outer_radius = float(parameters["outerRadius"])
    offset_x = float(parameters["innerOffsetX"])
    offset_y = float(parameters["innerOffsetY"])
    _positive(width, height, wall, flange_length, flange_thickness)
    if outer_radius < 0 or not math.isfinite(outer_radius):
        raise ValueError("外 R 必须为非负有限数值")
    if flange_root_radius < 0 or not math.isfinite(flange_root_radius):
        raise ValueError("安装翼根部 R 必须为非负有限数值")
    if flange_thickness > wall + 1e-9:
        raise ValueError("翼板厚度不能大于管体壁厚")
    if width <= 2 * wall or height <= 2 * wall:
        raise ValueError("管体宽高必须大于两倍壁厚")
    if abs(offset_x) >= wall or abs(offset_y) >= wall:
        raise ValueError("管体内轮廓偏心不能达到壁厚")

    if parameters["useIndependentOuterRadii"]:
        outer_radii = [float(parameters[f"outerRadius{index}"]) for index in range(1, 4)]
    else:
        outer_radii = [outer_radius] * 3
    if any(not math.isfinite(value) or value < 0 for value in outer_radii):
        raise ValueError("独立外 R 必须为非负有限数值")
    if parameters["useIndependentInnerRadii"]:
        inner_radii = [float(parameters[f"innerRadius{index}"]) for index in range(1, 5)]
    else:
        inner_radii = [0.0, max(outer_radii[0] - wall, 0.0),
                       max(outer_radii[1] - wall, 0.0), max(outer_radii[2] - wall, 0.0)]
    if any(not math.isfinite(value) or value < 0 for value in inner_radii):
        raise ValueError("独立内 R 必须为非负有限数值")

    left, right = -width / 2, width / 2
    outer_points = [[left, -flange_length], [left + flange_thickness, -flange_length],
                    [left + flange_thickness, 0], [right, 0], [right, height], [left, height]]
    inner_width, inner_height = width - 2 * wall, height - 2 * wall
    inner_center = [offset_x, height / 2 + offset_y]
    inner_points = [[inner_center[0] - inner_width / 2, inner_center[1] - inner_height / 2],
                    [inner_center[0] + inner_width / 2, inner_center[1] - inner_height / 2],
                    [inner_center[0] + inner_width / 2, inner_center[1] + inner_height / 2],
                    [inner_center[0] - inner_width / 2, inner_center[1] + inner_height / 2]]
    return [_rounded_path(outer_points, [0, 0, flange_root_radius, *outer_radii]),
            _rounded_path(inner_points, inner_radii)]


def _mirror(contours):
    result = []
    for contour in contours:
        result.append({**contour, "segments": [
            {**segment, **{key: [-value[0], value[1]] for key, value in segment.items()
                           if key in ("start", "middle", "end")}}
            for segment in contour["segments"]
        ]})
    return result


def build(parameters):
    parameters = dict(parameters)
    contours = _shape(parameters)
    if parameters["mirrorX"]:
        contours = _mirror(contours)
    width = float(parameters["width"])
    height = float(parameters["depth"])
    flange_length = float(parameters["flangeLength"])
    overall_depth = height + flange_length
    wall = float(parameters["wallThickness"])
    return {
        "contours": contours,
        **parameters,
        "kind": "p-tube",
        "width": width,
        # depth 是生成截面的整体包络高度；输入参数 depth 仍由宿主在
        # parameters 中保留为管体外高，安装翼伸出单独保留。
        "depth": overall_depth,
        "wallThickness": wall,
        "cornerRadius": float(parameters["outerRadius"]),
        "specification": (f"P 型管 管体 {width:g} × {height:g} mm"
                          f" / 安装翼 {flange_length:g} mm"),
        "manufacturingRoute": "Parametric",
    }
