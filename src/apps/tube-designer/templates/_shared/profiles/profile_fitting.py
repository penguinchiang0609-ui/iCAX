from __future__ import annotations

import math
from typing import Any, Iterable


Point = tuple[float, float]


def _number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    return result


def _points(loop: dict[str, Any]) -> list[Point]:
    raw = loop.get("points", [])
    if not isinstance(raw, list):
        return []
    result: list[Point] = []
    for value in raw:
        if not isinstance(value, list) or len(value) != 2:
            continue
        point = (_number(value[0], "contour point"), _number(value[1], "contour point"))
        if not result or math.dist(point, result[-1]) > 1.0e-10:
            result.append(point)
    if len(result) > 1 and math.dist(result[0], result[-1]) <= 1.0e-10:
        result.pop()
    return result


def _area(points: list[Point]) -> float:
    return 0.5 * sum(
        points[index][0] * points[(index + 1) % len(points)][1]
        - points[(index + 1) % len(points)][0] * points[index][1]
        for index in range(len(points))
    ) if len(points) >= 3 else 0.0


def _bbox(points: Iterable[Point]) -> tuple[float, float, float, float]:
    values = list(points)
    return (
        min(point[0] for point in values),
        max(point[0] for point in values),
        min(point[1] for point in values),
        max(point[1] for point in values),
    )


def _center(points: list[Point]) -> Point:
    minimum_x, maximum_x, minimum_y, maximum_y = _bbox(points)
    return ((minimum_x + maximum_x) / 2.0, (minimum_y + maximum_y) / 2.0)


def _principal_axis(points: list[Point]) -> Point:
    center = _center(points)
    xx = sum((point[0] - center[0]) ** 2 for point in points)
    yy = sum((point[1] - center[1]) ** 2 for point in points)
    xy = sum((point[0] - center[0]) * (point[1] - center[1]) for point in points)
    angle = 0.5 * math.atan2(2.0 * xy, xx - yy)
    axis = (math.cos(angle), math.sin(angle))
    if axis[0] < -1.0e-12 or (abs(axis[0]) <= 1.0e-12 and axis[1] < 0.0):
        axis = (-axis[0], -axis[1])
    return axis


def _frame_candidates(points: list[Point]) -> list[tuple[Point, Point]]:
    axes: list[Point] = [_principal_axis(points)]
    # Polygonal profiles are best aligned from their actual straight-edge
    # directions. Include those directions in addition to the PCA direction.
    for first, second in zip(points, points[1:] + points[:1]):
        dx, dy = second[0] - first[0], second[1] - first[1]
        length = math.hypot(dx, dy)
        if length > 1.0e-9:
            candidate = (dx / length, dy / length)
            if candidate[0] < -1.0e-12 or (
                abs(candidate[0]) <= 1.0e-12 and candidate[1] < 0.0
            ):
                candidate = (-candidate[0], -candidate[1])
            if all(abs(candidate[0] * axis[1] - candidate[1] * axis[0]) > 1.0e-6
                   for axis in axes):
                axes.append(candidate)
    result: list[tuple[Point, Point]] = []
    for axis in axes:
        for primary in (axis, (-axis[0], -axis[1])):
            result.append((primary, (-primary[1], primary[0])))
            result.append(((-primary[1], primary[0]), (primary[0], primary[1])))
    return result


def _to_frame(points: list[Point], basis: tuple[Point, Point]) -> tuple[list[Point], Point]:
    u, v = basis
    minimum_x = min(point[0] * u[0] + point[1] * u[1] for point in points)
    maximum_x = max(point[0] * u[0] + point[1] * u[1] for point in points)
    minimum_y = min(point[0] * v[0] + point[1] * v[1] for point in points)
    maximum_y = max(point[0] * v[0] + point[1] * v[1] for point in points)
    center = (
        0.5 * (minimum_x + maximum_x),
        0.5 * (minimum_y + maximum_y),
    )
    return [
        (
            point[0] * u[0] + point[1] * u[1] - center[0],
            point[0] * v[0] + point[1] * v[1] - center[1],
        )
        for point in points
    ], center


def _boundary_distance(point: Point, polygon: list[Point]) -> float:
    result = float("inf")
    for first, second in zip(polygon, polygon[1:] + polygon[:1]):
        dx, dy = second[0] - first[0], second[1] - first[1]
        length_squared = dx * dx + dy * dy
        if length_squared <= 1.0e-18:
            result = min(result, math.dist(point, first))
            continue
        u = max(0.0, min(1.0, (
            (point[0] - first[0]) * dx + (point[1] - first[1]) * dy
        ) / length_squared))
        result = min(result, math.dist(point, (first[0] + u * dx, first[1] + u * dy)))
    return result


def _symmetric_error(first: list[Point], second: list[Point]) -> float:
    if not first or not second:
        return float("inf")
    return max(
        max(_boundary_distance(point, second) for point in first),
        max(_boundary_distance(point, first) for point in second),
    )


def _straight_side_score(points: list[Point], tolerance: float) -> float:
    minimum_x, maximum_x, minimum_y, maximum_y = _bbox(points)
    width, height = maximum_x - minimum_x, maximum_y - minimum_y
    score = 0.0
    for side, denominator in (
        (lambda point: abs(point[0] - minimum_x), max(height, 1.0e-9)),
        (lambda point: abs(point[0] - maximum_x), max(height, 1.0e-9)),
        (lambda point: abs(point[1] - minimum_y), max(width, 1.0e-9)),
        (lambda point: abs(point[1] - maximum_y), max(width, 1.0e-9)),
    ):
        support = [point for point in points if side(point) <= tolerance]
        if len(support) >= 2:
            score += (max(point[1] for point in support) - min(point[1] for point in support)) / denominator
            score += (max(point[0] for point in support) - min(point[0] for point in support)) / denominator
    return score


def _circle_fit(points: list[Point]) -> tuple[Point, float, float]:
    center = _center(points)
    radii = [math.dist(point, center) for point in points]
    radius = sum(radii) / len(radii)
    residual = max(abs(value - radius) for value in radii)
    return center, radius, residual


def _make_trsf(center: Point, basis: tuple[Point, Point]) -> list[list[float]]:
    u, v = basis
    return [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, u[0], u[1], -(center[0] * u[0] + center[1] * u[1])],
        [0.0, v[0], v[1], -(center[0] * v[0] + center[1] * v[1])],
        [0.0, 0.0, 0.0, 1.0],
    ]


def _result(
    parameters: dict[str, Any], center: Point, basis: tuple[Point, Point],
) -> dict[str, Any]:
    return {
        "matched": True,
        "parameters": parameters,
        "trsf": _make_trsf(center, basis),
    }


def _fit_round(
    outer: list[Point], inner: list[Point], tolerance: float,
) -> dict[str, Any]:
    if not inner:
        return {"matched": False}
    outer_center, outer_radius, outer_error = _circle_fit(outer)
    inner_center, inner_radius, inner_error = _circle_fit(inner)
    scale = max(outer_radius, 1.0)
    if max(outer_error, inner_error, math.dist(outer_center, inner_center)) > max(tolerance * 8.0, scale * 1.0e-3):
        return {"matched": False}
    if inner_radius <= 0.0 or inner_radius >= outer_radius:
        return {"matched": False}
    return _result(
        {
            "width": 2.0 * outer_radius,
            "depth": 2.0 * outer_radius,
            "wallThickness": outer_radius - inner_radius,
            "cornerRadius": 0.0,
        },
        outer_center,
        ((1.0, 0.0), (0.0, 1.0)),
    )


def _fit_rect(
    outer: list[Point], inner: list[Point], tolerance: float,
) -> dict[str, Any]:
    if not inner:
        return {"matched": False}
    for basis in _frame_candidates(outer):
        outer_frame, center = _to_frame(outer, basis)
        inner_frame, inner_center = _to_frame(inner, basis)
        outer_min_x, outer_max_x, outer_min_y, outer_max_y = _bbox(outer_frame)
        inner_min_x, inner_max_x, inner_min_y, inner_max_y = _bbox(inner_frame)
        width, depth = outer_max_x - outer_min_x, outer_max_y - outer_min_y
        inner_width, inner_depth = inner_max_x - inner_min_x, inner_max_y - inner_min_y
        scale = max(width, depth, 1.0)
        local_tolerance = max(tolerance * 8.0, scale * 1.0e-3)
        if min(width, depth, inner_width, inner_depth) <= local_tolerance:
            continue
        if _straight_side_score(outer_frame, local_tolerance) < 1.0:
            continue
        if _straight_side_score(inner_frame, local_tolerance) < 1.0:
            continue
        if math.dist(center, inner_center) > local_tolerance:
            continue
        wall_width = (width - inner_width) / 2.0
        wall_depth = (depth - inner_depth) / 2.0
        if wall_width <= local_tolerance or wall_depth <= local_tolerance:
            continue
        if abs(wall_width - wall_depth) > local_tolerance:
            continue
        return _result(
            {
                "width": width,
                "depth": depth,
                "wallThickness": 0.5 * (wall_width + wall_depth),
                "cornerRadius": 0.0,
            },
            center,
            basis,
        )
    return {"matched": False}


def _canonical_points(
    profile_id: str, width: float, depth: float, thickness: float,
) -> list[Point] | None:
    half_width, half_depth = width / 2.0, depth / 2.0
    if profile_id == "angle":
        return [
            (-half_width, -half_depth), (half_width, -half_depth),
            (half_width, -half_depth + thickness),
            (-half_width + thickness, -half_depth + thickness),
            (-half_width + thickness, half_depth), (-half_width, half_depth),
        ]
    if profile_id == "channel":
        return [
            (-half_width, -half_depth), (half_width, -half_depth),
            (half_width, -half_depth + thickness),
            (-half_width + thickness, -half_depth + thickness),
            (-half_width + thickness, half_depth - thickness),
            (half_width, half_depth - thickness), (half_width, half_depth),
            (-half_width, half_depth),
        ]
    if profile_id == "i-section":
        half_web = thickness / 2.0
        return [
            (-half_width, -half_depth), (half_width, -half_depth),
            (half_width, -half_depth + thickness), (half_web, -half_depth + thickness),
            (half_web, half_depth - thickness), (half_width, half_depth - thickness),
            (half_width, half_depth), (-half_width, half_depth),
            (-half_width, half_depth - thickness), (-half_web, half_depth - thickness),
            (-half_web, -half_depth + thickness), (-half_width, -half_depth + thickness),
        ]
    if profile_id == "t-section":
        half_web = thickness / 2.0
        return [
            (-half_width, half_depth - thickness), (-half_web, half_depth - thickness),
            (-half_web, -half_depth), (half_web, -half_depth),
            (half_web, half_depth - thickness), (half_width, half_depth - thickness),
            (half_width, half_depth), (-half_width, half_depth),
        ]
    if profile_id == "z-section":
        half_web = thickness / 2.0
        return [
            (-half_width, -half_depth), (half_web, -half_depth),
            (half_web, half_depth - thickness), (half_width, half_depth - thickness),
            (half_width, half_depth), (-half_web, half_depth),
            (-half_web, -half_depth + thickness), (-half_width, -half_depth + thickness),
        ]
    return None


def _fit_canonical_polygon(
    profile_id: str, outer: list[Point], tolerance: float,
) -> dict[str, Any]:
    for basis in _frame_candidates(outer):
        frame, center = _to_frame(outer, basis)
        minimum_x, maximum_x, minimum_y, maximum_y = _bbox(frame)
        width, depth = maximum_x - minimum_x, maximum_y - minimum_y
        scale = max(width, depth, 1.0)
        limit = max(tolerance * 10.0, scale * 2.0e-3)
        for index in range(1, 80):
            thickness = min(width, depth) * (0.01 + 0.48 * index / 80.0)
            canonical = _canonical_points(profile_id, width, depth, thickness)
            if canonical is None:
                return {"matched": False}
            if _symmetric_error(frame, canonical) <= limit:
                return _result(
                    {
                        "width": width,
                        "depth": depth,
                        "wallThickness": thickness,
                        "cornerRadius": 0.0,
                    },
                    center,
                    basis,
                )
    return {"matched": False}


def _fit_ellipse(
    outer: list[Point], inner: list[Point], tolerance: float,
) -> dict[str, Any]:
    if not inner:
        return {"matched": False}
    for basis in _frame_candidates(outer):
        outer_frame, center = _to_frame(outer, basis)
        inner_frame, inner_center = _to_frame(inner, basis)
        outer_min_x, outer_max_x, outer_min_y, outer_max_y = _bbox(outer_frame)
        inner_min_x, inner_max_x, inner_min_y, inner_max_y = _bbox(inner_frame)
        width, depth = outer_max_x - outer_min_x, outer_max_y - outer_min_y
        inner_width, inner_depth = inner_max_x - inner_min_x, inner_max_y - inner_min_y
        if min(width, depth, inner_width, inner_depth) <= 1.0e-9:
            continue
        scale = max(width, depth, 1.0)
        error = max(
            abs((point[0] / (width / 2.0)) ** 2 + (point[1] / (depth / 2.0)) ** 2 - 1.0)
            for point in outer_frame
        ) * scale
        if error > max(tolerance * 10.0, scale * 2.0e-3):
            continue
        if math.dist(center, inner_center) > max(tolerance * 10.0, scale * 2.0e-3):
            continue
        wall_width = (width - inner_width) / 2.0
        wall_depth = (depth - inner_depth) / 2.0
        if wall_width <= 0.0 or wall_depth <= 0.0:
            continue
        if abs(wall_width - wall_depth) > max(tolerance * 10.0, scale * 2.0e-3):
            continue
        return _result(
            {
                "width": width,
                "depth": depth,
                "wallThickness": 0.5 * (wall_width + wall_depth),
                "cornerRadius": 0.0,
            },
            center,
            basis,
        )
    return {"matched": False}


def fitter_for_profile(
    contours: list[dict[str, Any]], profile_id: str, context: dict[str, Any] | None = None,
) -> dict[str, Any]:
    context = context or {}
    tolerance = _number(context.get("linearTolerance", 0.001), "linearTolerance")
    loops = [(_points(loop), bool(loop.get("inner", False))) for loop in contours]
    loops = [(points, inner) for points, inner in loops if len(points) >= 3]
    if not loops:
        return {"matched": False}
    outer = next((points for points, inner in loops if not inner), loops[0][0])
    inner = [points for points, inner_flag in loops if inner_flag]
    if profile_id == "round":
        return _fit_round(outer, inner[0] if inner else [], tolerance)
    if profile_id == "rect":
        return _fit_rect(outer, inner[0] if inner else [], tolerance)
    if profile_id in ("ellipse",):
        return _fit_ellipse(outer, inner[0] if inner else [], tolerance)
    if profile_id in ("channel", "angle", "i-section", "t-section", "z-section"):
        return _fit_canonical_polygon(profile_id, outer, tolerance)
    # Polygon and flat-oval use distinct profile scripts and can add a more
    # specialized fitter later without changing the C++ protocol.
    return {"matched": False}
