"""Generic D-tube profile: an outer D and a translated inner D path."""
import math


PARAMETERS = ("width", "wallThickness", "innerOffsetX", "innerOffsetY")


def _shape(parameters):
    radius = float(parameters["width"])
    wall = float(parameters["wallThickness"])
    offset_x = float(parameters.get("innerOffsetX", 0))
    offset_y = float(parameters.get("innerOffsetY", 0))
    if not all(math.isfinite(value) for value in (radius, wall, offset_x, offset_y)):
        raise ValueError("尺寸必须为有限数值")
    if radius <= 2 * wall or wall <= 0:
        raise ValueError("D 型管外宽必须大于两倍壁厚")
    if math.hypot(offset_x, offset_y) >= wall:
        raise ValueError("内孔水平、竖直偏心合成距离必须小于壁厚")
    outer_center = -radius / 2
    inner_radius = radius - wall
    inner_center = outer_center + offset_x
    inner_center_y = offset_y
    chord_height = math.sqrt(inner_radius * inner_radius - wall * wall)
    outer = {
        "kind": "path",
        "closed": True,
        "segments": [
            {"kind": "line", "start": [outer_center, -radius], "end": [outer_center, radius]},
            {"kind": "arc", "start": [outer_center, radius], "middle": [outer_center + radius, 0], "end": [outer_center, -radius]},
        ],
    }
    inner_chord = inner_center + wall
    inner = {
        "kind": "path",
        "closed": True,
        "segments": [
            {"kind": "line", "start": [inner_chord, inner_center_y - chord_height], "end": [inner_chord, inner_center_y + chord_height]},
            {"kind": "arc", "start": [inner_chord, inner_center_y + chord_height], "middle": [inner_center + inner_radius, inner_center_y], "end": [inner_chord, inner_center_y - chord_height]},
        ],
    }
    return [outer, inner]


def build(parameters):
    parameters = dict(parameters)
    contours = _shape(parameters)
    radius = float(parameters["width"])
    return {
        "contours": contours,
        **parameters,
        "_parameters": parameters,
        "kind": "curve-hollow",
        "width": radius,
        "depth": 2 * radius,
        "wallThickness": float(parameters["wallThickness"]),
        "cornerRadius": 0,
        "specification": f"D 型管 {radius:g} × {2 * radius:g} mm",
        "manufacturingRoute": "Parametric",
    }
