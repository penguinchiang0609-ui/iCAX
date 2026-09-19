"""Direct inverse for the outer and translated-inner D paths."""
import math


IMPLEMENTED = True


def fitting(section, context):
    q, geometry, tolerance = context["geometry"], context["curves"], context["tolerance"]
    if len(section) != 2:
        return False
    for loops, pose in geometry.frames(section):
        measured = []
        for loop in loops:
            if loop["kind"] != "path" or len(loop["edges"]) != 2:
                break
            lines = [edge for edge in loop["edges"] if edge["kind"] == "line"]
            arcs = [edge for edge in loop["edges"] if edge["kind"] == "arc"]
            if len(lines) != 1 or len(arcs) != 1:
                break
            line, arc = lines[0], arcs[0]
            start, end = line["start"], line["end"]
            center = arc["center"]
            radius = arc["radius"]
            if abs(start[0] - end[0]) > tolerance or abs(start[1] + end[1] - 2 * center[1]) > tolerance:
                break
            if radius <= tolerance or arc["sweep"] <= 0:
                break
            midpoint_angle = math.atan2(arc["start"][1] - center[1], arc["start"][0] - center[0]) + arc["sweep"] / 2
            if abs(math.sin(midpoint_angle) * radius) > tolerance or math.cos(midpoint_angle) < 0:
                break
            measured.append((arc, start[0]))
        if len(measured) != 2:
            continue
        (outer, outer_line), (inner, inner_line) = sorted(measured, key=lambda item: item[0]["radius"], reverse=True)
        outer_radius = outer["radius"]
        inner_radius = inner["radius"]
        wall = outer_radius - inner_radius
        offset_x = inner["center"][0] - outer["center"][0]
        offset_y = inner["center"][1] - outer["center"][1]
        if wall <= tolerance or outer_radius - 2 * wall <= tolerance:
            continue
        if math.hypot(offset_x, offset_y) >= wall - tolerance:
            continue
        if abs(inner_line - outer_line - wall - offset_x) > tolerance:
            continue
        if abs(outer["sweep"] - math.pi) * outer_radius > tolerance:
            continue
        return q.result({"width": outer_radius, "wallThickness": wall, "innerOffsetX": offset_x, "innerOffsetY": offset_y}, pose, "unique")
    return False
