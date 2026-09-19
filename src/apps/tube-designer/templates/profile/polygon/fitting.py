"""Direct geometric inverse for the generic regular-polygon tube."""
import math


def _ordered_radii(corners, vertices, tolerance):
    base = vertices["phase"]

    def position(corner):
        angle = math.atan2(
            corner[0][1] - vertices["center"][1],
            corner[0][0] - vertices["center"][0],
        )
        return (angle - base) % math.tau

    ordered = sorted(corners, key=position)
    return [max(float(corner[1]), 0.0) for corner in ordered]


def fitting(section, context):
    q = context["geometry"]
    tolerance = context["tolerance"]
    if not isinstance(section, list) or len(section) != 2:
        return False

    outer_corners = q.polygon_corners(section[0], tolerance)
    inner_corners = q.polygon_corners(section[1], tolerance)
    if not outer_corners or not inner_corners:
        return False
    if len(outer_corners) != len(inner_corners):
        return False
    sides = len(outer_corners)
    if sides < 3 or sides > 32:
        return False

    outer = q.regular_vertices([corner[0] for corner in outer_corners], tolerance)
    inner = q.regular_vertices([corner[0] for corner in inner_corners], tolerance)
    if not outer or not inner:
        return False
    if outer["radius"] - inner["radius"] <= tolerance:
        return False

    wall = (outer["radius"] - inner["radius"]) * math.cos(math.pi / sides)
    if wall <= tolerance:
        return False
    offset = [inner["center"][i] - outer["center"][i] for i in (0, 1)]

    outer_radii = _ordered_radii(outer_corners, outer, tolerance)
    inner_radii = _ordered_radii(inner_corners, inner, tolerance)
    outer_radius = sum(outer_radii) / sides
    inner_radius = sum(inner_radii) / sides
    outer_independent = max(outer_radii) - min(outer_radii) > tolerance
    inner_independent = max(inner_radii) - min(inner_radii) > tolerance

    parameters = {
        "sideCount": sides,
        "radius": outer["radius"],
        "wallThickness": wall,
        "outerRadius": outer_radius,
        "innerOffsetX": offset[0],
        "innerOffsetY": offset[1],
        "useIndependentOuterRadii": outer_independent,
        "outerRadii": ",".join(format(value, ".12g") for value in outer_radii),
        "useIndependentInnerRadii": inner_independent,
        "innerRadii": ",".join(format(value, ".12g") for value in inner_radii),
    }
    if not outer_independent:
        parameters["outerRadius"] = outer_radius
    if not inner_independent:
        parameters["innerRadii"] = ""
    return q.result(parameters, {
        "rotation": outer["phase"],
        "translation": outer["center"],
    })
