"""Direct inverse for the generic single-flange P-tube path."""
import math


IMPLEMENTED = True


def _canonical(corners, mirror):
    if mirror:
        return [([-point[0], point[1]], radius) for point, radius in reversed(corners)]
    return [([point[0], point[1]], radius) for point, radius in corners]


def _rotations(corners, count, tolerance):
    for shift in range(count):
        current = corners[shift:] + corners[:shift]
        points = [corner[0] for corner in current]
        radii = [abs(float(corner[1])) for corner in current]
        pairs = tuple((index, index + 1) for index in range(0, count, 2))
        if any(abs(points[a][1] - points[b][1]) > tolerance for a, b in pairs):
            continue
        cross_pairs = tuple((index, (index + 1) % count) for index in range(1, count, 2))
        if any(abs(points[a][0] - points[b][0]) > tolerance for a, b in cross_pairs):
            continue
        yield points, radii


def _fit(outer_corners, inner_corners, tolerance):
    for mirror in (False, True):
        outer = _canonical(outer_corners, mirror)
        inner = _canonical(inner_corners, mirror)
        for points, outer_radii in _rotations(outer, 6, tolerance):
            p = points
            if any(value > tolerance for value in outer_radii[:2]):
                continue
            if p[1][0] <= p[0][0] + tolerance or p[2][1] <= p[1][1] + tolerance:
                continue
            if p[3][0] <= p[2][0] + tolerance or p[4][1] <= p[3][1] + tolerance:
                continue
            if p[5][0] >= p[4][0] - tolerance or p[0][1] >= p[5][1] - tolerance:
                continue

            width = p[3][0] - p[0][0]
            depth = p[4][1] - p[3][1]
            flange_length = p[2][1] - p[1][1]
            flange_thickness = p[1][0] - p[0][0]
            if min(width, depth, flange_length, flange_thickness) <= tolerance:
                continue

            outer_center = [(p[0][0] + p[3][0]) / 2,
                            (p[3][1] + p[4][1]) / 2]
            for inner_points, inner_radii in _rotations(inner, 4, tolerance):
                ip = inner_points
                inner_width = ip[1][0] - ip[0][0]
                inner_depth = ip[2][1] - ip[1][1]
                if min(inner_width, inner_depth) <= tolerance:
                    continue
                wall_x = (width - inner_width) / 2
                wall_y = (depth - inner_depth) / 2
                if min(wall_x, wall_y) <= tolerance or abs(wall_x - wall_y) > tolerance:
                    continue
                wall = wall_x
                if flange_thickness > wall + tolerance:
                    continue
                inner_center = [(ip[0][0] + ip[1][0]) / 2,
                                (ip[1][1] + ip[2][1]) / 2]
                offset_x = inner_center[0] - outer_center[0]
                offset_y = inner_center[1] - outer_center[1]
                if abs(offset_x) >= wall - tolerance or abs(offset_y) >= wall - tolerance:
                    continue

                outer_values = outer_radii[3:6]
                independent_outer = max(outer_values) - min(outer_values) > tolerance
                derived_inner = [0.0, max(outer_values[0] - wall, 0.0),
                                 max(outer_values[1] - wall, 0.0),
                                 max(outer_values[2] - wall, 0.0)]
                independent_inner = any(abs(a - b) > tolerance
                                        for a, b in zip(inner_radii, derived_inner))
                parameters = {
                    "width": width,
                    "depth": depth,
                    "wallThickness": wall,
                    "flangeLength": flange_length,
                    "flangeThickness": flange_thickness,
                    "flangeRootRadius": outer_radii[2],
                    "outerRadius": outer_values[0] if independent_outer else sum(outer_values) / 3,
                    "mirrorX": mirror,
                    "innerOffsetX": offset_x,
                    "innerOffsetY": offset_y,
                    "useIndependentOuterRadii": independent_outer,
                    "outerRadius1": outer_values[0],
                    "outerRadius2": outer_values[1],
                    "outerRadius3": outer_values[2],
                    "useIndependentInnerRadii": independent_inner,
                    "innerRadius1": inner_radii[0],
                    "innerRadius2": inner_radii[1],
                    "innerRadius3": inner_radii[2],
                    "innerRadius4": inner_radii[3],
                }
                # frames() centers the imported envelope. The P profile origin
                # is the body center at y=0, so restore that anchor before
                # returning the pose used by the host.
                origin = [-outer_center[0] if mirror else outer_center[0], p[3][1]]
                return parameters, origin
    return False


def fitting(section, context):
    q, geometry, tolerance = context["geometry"], context["curves"], context["tolerance"]
    if len(section) != 2:
        return False
    for loops, pose in geometry.frames(section):
        outer = q.polygon_corners(loops[0], tolerance)
        inner = q.polygon_corners(loops[1], tolerance)
        if not outer or not inner or len(outer) != 6 or len(inner) != 4:
            continue
        result = _fit(outer, inner, tolerance)
        if result:
            parameters, origin = result
            return q.result(parameters, q.shifted_pose(pose, origin), "unique")
    return False
