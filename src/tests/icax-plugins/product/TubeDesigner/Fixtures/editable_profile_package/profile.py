from __future__ import annotations


def build(parameters):
    width = float(parameters["width"])
    depth = float(parameters["depth"])
    wall = float(parameters["wallThickness"])
    if wall <= 0 or wall * 2 >= min(width, depth):
        raise ValueError("壁厚无效")
    return {
        "kind": "editable-rect",
        "width": width,
        "depth": depth,
        "wallThickness": wall,
        "cornerRadius": 0.0,
        "specification": f"{width:g} × {depth:g} × {wall:g}",
    }


def contours(profile, *, clearance=0.0, swap_axes=False):
    width = float(profile["width"])
    depth = float(profile["depth"])
    wall = float(profile["wallThickness"])
    if swap_axes:
        width, depth = depth, width
    return [
        {"kind": "roundedRectangle", "width": width + clearance * 2, "height": depth + clearance * 2, "radius": 0.0},
        {"kind": "roundedRectangle", "width": width - wall * 2, "height": depth - wall * 2, "radius": 0.0},
    ]
