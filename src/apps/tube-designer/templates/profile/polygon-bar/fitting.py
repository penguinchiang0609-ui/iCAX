"""Direct geometric inverse for a regular solid polygon with equal corner radii."""
IMPLEMENTED = True


def fitting(section, context):
    q = context["geometry"]
    tolerance = context["tolerance"]
    if len(section) != 1:
        return False
    corners = q.polygon_corners(section[0], tolerance)
    if not corners or not 3 <= len(corners) <= 32:
        return False
    regular = q.regular_vertices([corner[0] for corner in corners], tolerance)
    if not regular:
        return False
    radii = [float(corner[1]) for corner in corners]
    if any(radius < 0 for radius in radii):
        return False
    corner_radius = sum(radii) / len(radii)
    if max(radii) - min(radii) > tolerance:
        return False
    return q.result({
        "radius": regular["radius"],
        "sideCount": len(corners),
        "cornerRadius": corner_radius,
    }, {
        "rotation": regular["phase"],
        "translation": regular["center"],
    })
