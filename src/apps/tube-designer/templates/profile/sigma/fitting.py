"""Direct inverse for the symmetric/uniform-radius Sigma shortcut."""
import math

IMPLEMENTED = True


def _angle(a, b):
    la, lb = math.hypot(*a), math.hypot(*b)
    if min(la, lb) <= 1e-12:
        return None
    cosine = max(-1.0, min(1.0, sum(a[i] * b[i] for i in (0, 1)) / (la * lb)))
    return math.degrees(math.acos(cosine))


def fitting(section, context):
    q, geometry, tolerance = context["geometry"], context["curves"], context["tolerance"]
    if len(section) != 1:
        return False
    for loops, pose in geometry.frames(section):
        for raw, wall, radius in q.strip_axes(loops[0], tolerance):
            for points in (raw, list(reversed(raw))):
                if len(points) != 10:
                    continue
                p0, p1, p2, p3, p4, p5, p6, p7, p8, p9 = points
                if (abs(p1[1] - p2[1]) > tolerance
                        or abs(p2[0] - p3[0]) > tolerance
                        or abs(p4[0] - p5[0]) > tolerance
                        or abs(p6[0] - p7[0]) > tolerance
                        or abs(p7[1] - p8[1]) > tolerance):
                    continue
                top_b, bottom_b = p1[0] - p2[0], p8[0] - p7[0]
                top_a, bottom_a = p2[1] - p3[1], p6[1] - p7[1]
                top_q, bottom_q = p3[1] - p4[1], p5[1] - p6[1]
                offset_top, offset_bottom = p4[0] - p3[0], p5[0] - p6[0]
                height = p2[1] - p7[1] + wall
                center_height = p4[1] - p5[1]
                if min(top_b, bottom_b, top_a, bottom_a, top_q, bottom_q,
                       height, center_height, wall) <= tolerance:
                    continue
                if abs(offset_top - offset_bottom) > tolerance:
                    continue
                top_c, bottom_c = math.dist(p0, p1), math.dist(p8, p9)
                top_theta = _angle([p2[i] - p1[i] for i in (0, 1)],
                                   [p0[i] - p1[i] for i in (0, 1)])
                bottom_theta = _angle([p7[i] - p8[i] for i in (0, 1)],
                                      [p9[i] - p8[i] for i in (0, 1)])
                if top_theta is None or bottom_theta is None:
                    continue
                independent = any(abs(a - b) > tolerance for a, b in (
                    (top_b, bottom_b), (top_c, bottom_c), (top_a, bottom_a),
                    (top_q, bottom_q), (top_theta, bottom_theta)))
                params = {
                    "depth": height,
                    "flangeWidth": (top_b + bottom_b) / 2,
                    "lipLength": (top_c + bottom_c) / 2,
                    "wallThickness": wall,
                    "bendRadius": radius,
                    "centerWebOffset": (offset_top + offset_bottom) / 2,
                    "outerWebHeight": (top_a + bottom_a) / 2,
                    "transitionRise": (top_q + bottom_q) / 2,
                    "lipAngle": (top_theta + bottom_theta) / 2,
                    "useIndependentSides": independent,
                    "topFlangeWidth": top_b,
                    "bottomFlangeWidth": bottom_b,
                    "topLipLength": top_c,
                    "bottomLipLength": bottom_c,
                    "topOuterWebHeight": top_a,
                    "bottomOuterWebHeight": bottom_a,
                    "upperTransitionRise": top_q,
                    "lowerTransitionRise": bottom_q,
                    "topLipAngle": top_theta,
                    "bottomLipAngle": bottom_theta,
                    "useIndependentRadii": False,
                    **{f"bendRadius{index}": radius for index in range(1, 9)},
                    "mirrorX": False,
                    "mirrorY": False,
                }
                origin = [p2[0], (p2[1] + p7[1]) / 2]
                return q.result(params, q.shifted_pose(pose, origin))
    return False
