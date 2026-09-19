"""Direct inverse for the generic T-section path."""


IMPLEMENTED = True


def fitting(section, context):
    q, geometry, tolerance = context["geometry"], context["curves"], context["tolerance"]
    if len(section) != 1:
        return False
    for loops, pose in geometry.frames(section):
        corners = q.polygon_corners(loops[0], tolerance, chamfers=True)
        if not corners or len(corners) != 8:
            continue
        for shift in range(8):
            current = corners[shift:] + corners[:shift]
            x = [corner[0][0] for corner in current]
            y = [corner[0][1] for corner in current]
            radii = [abs(corner[1]) for corner in current]
            if any(abs(y[a] - y[b]) > tolerance for a, b in ((0, 1), (0, 4), (0, 5), (2, 3), (6, 7))):
                continue
            if any(abs(x[a] - x[b]) > tolerance for a, b in ((1, 2), (3, 4), (5, 6), (7, 0))):
                continue
            width = x[5] - x[0]
            depth = y[6] - y[2]
            wall = x[3] - x[2]
            flange = y[6] - y[5]
            offset = (x[2] + x[3] - x[0] - x[5]) / 2
            if min(width - wall - 2 * abs(offset), depth - flange, wall, flange) <= tolerance:
                continue
            if any(radii[index] > tolerance for index in (0, 2, 3, 5, 6, 7)):
                continue
            left_radius = radii[1]
            right_radius = radii[4]
            if left_radius >= min(abs(x[1] - x[0]), abs(y[1] - y[2])) - tolerance:
                continue
            if right_radius >= min(abs(x[5] - x[4]), abs(y[4] - y[3])) - tolerance:
                continue
            independent = abs(left_radius - right_radius) > tolerance
            origin = [(x[0] + x[5]) / 2, (y[2] + y[6]) / 2]
            parameters = {
                "width": width,
                "depth": depth,
                "rootRadius": left_radius,
                "wallThickness": wall,
                "flangeThickness": flange,
                # A flat split face is geometrically identical to the generic
                # T profile.  The inverse therefore keeps the safe default;
                # the hot-rolled semantic switch is not recoverable from
                # contour geometry alone.
                "useHotRolled": False,
                "webOffset": offset,
                "useIndependentRadii": independent,
                "rootRadius1": left_radius,
                "rootRadius2": right_radius,
            }
            return q.result(parameters, q.shifted_pose(pose, origin))
    return False
