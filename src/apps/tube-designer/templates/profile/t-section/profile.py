"""Generic T-section profile built from one reusable line/arc path."""
import math


PARAMETERS = (
    "width", "depth", "rootRadius", "wallThickness", "flangeThickness",
    "useHotRolled", "webOffset", "useIndependentRadii", "rootRadius1", "rootRadius2",
)


def _positive(*values):
    if any(not math.isfinite(float(value)) or float(value) <= 1e-6 for value in values):
        raise ValueError("尺寸必须为大于零的有限数值")


def _path(points, radii):
    if len(points) != 8 or len(radii) != 8:
        raise ValueError("T 型钢轮廓必须有 8 个顶点")
    corners = []
    for index, point in enumerate(points):
        before = points[index - 1]
        after = points[(index + 1) % len(points)]
        incoming = [point[0] - before[0], point[1] - before[1]]
        outgoing = [after[0] - point[0], after[1] - point[1]]
        incoming_length = math.hypot(*incoming)
        outgoing_length = math.hypot(*outgoing)
        _positive(incoming_length, outgoing_length)
        incoming = [value / incoming_length for value in incoming]
        outgoing = [value / outgoing_length for value in outgoing]
        turn = math.atan2(
            incoming[0] * outgoing[1] - incoming[1] * outgoing[0],
            incoming[0] * outgoing[0] + incoming[1] * outgoing[1],
        )
        radius = float(radii[index])
        if not math.isfinite(radius) or radius < 0:
            raise ValueError("根部 R 不能为负数")
        distance = radius * math.tan(abs(turn) / 2)
        if distance >= min(incoming_length, outgoing_length) - 1e-8:
            raise ValueError("根部 R 超过相邻直边长度")
        start = [point[j] - incoming[j] * distance for j in (0, 1)]
        end = [point[j] + outgoing[j] * distance for j in (0, 1)]
        middle = None
        if radius:
            sign = 1 if turn > 0 else -1
            center = [
                start[0] - incoming[1] * sign * radius,
                start[1] + incoming[0] * sign * radius,
            ]
            angle = math.atan2(start[1] - center[1], start[0] - center[0]) + turn / 2
            middle = [
                center[0] + radius * math.cos(angle),
                center[1] + radius * math.sin(angle),
            ]
        corners.append((start, end, middle))
    segments = []
    for index, corner in enumerate(corners):
        next_corner = corners[(index + 1) % len(corners)]
        segments.append({"kind": "line", "start": corner[1], "end": next_corner[0]})
        if next_corner[2] is not None:
            segments.append({
                "kind": "arc",
                "start": next_corner[0],
                "middle": next_corner[2],
                "end": next_corner[1],
            })
    return {"kind": "path", "closed": True, "segments": segments}


def _shape(parameters):
    width = float(parameters["width"])
    depth = float(parameters["depth"])
    wall = float(parameters["wallThickness"])
    flange = float(parameters["flangeThickness"])
    offset = float(parameters.get("webOffset", 0))
    _positive(width, depth, wall, flange)
    if flange >= depth:
        raise ValueError("翼板厚度必须小于总高")
    if abs(offset) + wall / 2 >= width / 2:
        raise ValueError("腹板偏距使腹板超出翼板")
    if parameters.get("useIndependentRadii", False):
        left_radius = float(parameters["rootRadius1"])
        right_radius = float(parameters["rootRadius2"])
    else:
        left_radius = right_radius = float(parameters["rootRadius"])
    web_left = offset - wall / 2
    web_right = offset + wall / 2
    flange_bottom = depth / 2 - flange
    points = [
        [-width / 2, flange_bottom],
        [web_left, flange_bottom],
        [web_left, -depth / 2],
        [web_right, -depth / 2],
        [web_right, flange_bottom],
        [width / 2, flange_bottom],
        [width / 2, depth / 2],
        [-width / 2, depth / 2],
    ]
    return _path(points, [0, left_radius, 0, 0, right_radius, 0, 0, 0])


def build(parameters):
    parameters = dict(parameters)
    contour = _shape(parameters)
    return {
        "contours": [contour],
        **parameters,
        "_parameters": parameters,
        "kind": "t-section",
        "width": float(parameters["width"]),
        "depth": float(parameters["depth"]),
        "wallThickness": float(parameters["wallThickness"]),
        "cornerRadius": float(parameters["rootRadius"]),
        "specification": f"T 型钢 {float(parameters['width']):g} × {float(parameters['depth']):g} mm",
        "manufacturingRoute": "HotRolledCutT" if parameters.get("useHotRolled",False) else "Parametric",
    }
