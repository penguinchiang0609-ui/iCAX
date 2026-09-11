from __future__ import annotations

import math
from typing import Any


Point = tuple[float, float]


def _number(parameters: dict[str, Any], key: str) -> float:
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} 必须是数值")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{key} 必须是有限数值")
    return result


def _integer(parameters: dict[str, Any], key: str) -> int:
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{key} 必须是整数")
    return value


def _text(value: float) -> str:
    return f"{value:.6f}".rstrip("0").rstrip(".")


def _cross(left: Point, right: Point) -> float:
    return left[0] * right[1] - left[1] * right[0]


def _subtract(left: Point, right: Point) -> Point:
    return left[0] - right[0], left[1] - right[1]


def _profile_points(
    width: float, depth: float, shape_mode: str, side_count: int,
    star_inner_ratio: float,
) -> list[Point]:
    if shape_mode == "regular":
        vertex_count = side_count
        start_angle = -math.pi / 2 - math.pi / side_count
        radii = [1.0] * vertex_count
    else:
        vertex_count = side_count * 2
        start_angle = -math.pi / 2
        radii = [1.0 if index % 2 == 0 else star_inner_ratio
                 for index in range(vertex_count)]

    raw_points = []
    for index, radius in enumerate(radii):
        angle = start_angle + math.tau * index / vertex_count
        raw_points.append((radius * math.cos(angle), radius * math.sin(angle)))

    min_x = min(point[0] for point in raw_points)
    max_x = max(point[0] for point in raw_points)
    min_y = min(point[1] for point in raw_points)
    max_y = max(point[1] for point in raw_points)
    center_x = (min_x + max_x) / 2
    center_y = (min_y + max_y) / 2
    scale_x = width / (max_x - min_x)
    scale_y = depth / (max_y - min_y)
    return [
        ((point[0] - center_x) * scale_x, (point[1] - center_y) * scale_y)
        for point in raw_points
    ]


def _offset_inward(points: list[Point], distance: float) -> list[Point]:
    result: list[Point] = []
    count = len(points)
    for index in range(count):
        previous = points[(index - 1) % count]
        current = points[index]
        following = points[(index + 1) % count]
        previous_direction = _subtract(current, previous)
        current_direction = _subtract(following, current)
        previous_length = math.hypot(*previous_direction)
        current_length = math.hypot(*current_direction)
        if previous_length <= 1.0e-9 or current_length <= 1.0e-9:
            raise ValueError("多边形管轮廓包含重合顶点")
        previous_normal = (
            -previous_direction[1] / previous_length,
            previous_direction[0] / previous_length,
        )
        current_normal = (
            -current_direction[1] / current_length,
            current_direction[0] / current_length,
        )
        previous_origin = (
            previous[0] + previous_normal[0] * distance,
            previous[1] + previous_normal[1] * distance,
        )
        current_origin = (
            current[0] + current_normal[0] * distance,
            current[1] + current_normal[1] * distance,
        )
        denominator = _cross(previous_direction, current_direction)
        if abs(denominator) <= 1.0e-10:
            raise ValueError("多边形管相邻边无法生成壁厚偏置")
        delta = _subtract(current_origin, previous_origin)
        factor = _cross(delta, current_direction) / denominator
        result.append((
            previous_origin[0] + previous_direction[0] * factor,
            previous_origin[1] + previous_direction[1] * factor,
        ))
    return result


def _signed_area(points: list[Point]) -> float:
    return sum(
        _cross(points[index], points[(index + 1) % len(points)])
        for index in range(len(points))
    ) / 2


def _orientation(first: Point, second: Point, third: Point) -> float:
    return _cross(_subtract(second, first), _subtract(third, first))


def _segments_intersect(
    first_start: Point, first_end: Point, second_start: Point, second_end: Point,
) -> bool:
    tolerance = 1.0e-8
    first_a = _orientation(first_start, first_end, second_start)
    first_b = _orientation(first_start, first_end, second_end)
    second_a = _orientation(second_start, second_end, first_start)
    second_b = _orientation(second_start, second_end, first_end)
    return (
        ((first_a > tolerance and first_b < -tolerance)
         or (first_a < -tolerance and first_b > tolerance))
        and ((second_a > tolerance and second_b < -tolerance)
             or (second_a < -tolerance and second_b > tolerance))
    )


def _is_simple(points: list[Point]) -> bool:
    count = len(points)
    for first in range(count):
        first_next = (first + 1) % count
        for second in range(first + 1, count):
            second_next = (second + 1) % count
            if first == second or first_next == second or second_next == first:
                continue
            if _segments_intersect(
                points[first], points[first_next], points[second], points[second_next],
            ):
                return False
    return True


def _contains_point(polygon: list[Point], point: Point) -> bool:
    inside = False
    previous = polygon[-1]
    for current in polygon:
        crosses = (current[1] > point[1]) != (previous[1] > point[1])
        if crosses:
            edge_x = (
                (previous[0] - current[0]) * (point[1] - current[1])
                / (previous[1] - current[1]) + current[0]
            )
            if point[0] < edge_x:
                inside = not inside
        previous = current
    return inside


def build(parameters: dict[str, Any]) -> dict[str, Any]:
    shape_mode = str(parameters["shapeMode"])
    side_count = _integer(parameters, "sideCount")
    star_inner_ratio = _number(parameters, "starInnerRatio")
    width = _number(parameters, "width")
    depth = _number(parameters, "depth")
    wall = _number(parameters, "wallThickness")
    if shape_mode not in ("regular", "star"):
        raise ValueError("多边形管轮廓类型无效")
    if side_count < 3 or side_count > 64:
        raise ValueError("多边形边数或星角数必须在 3 到 64 之间")
    if not 0.15 <= star_inner_ratio <= 0.9:
        raise ValueError("星谷半径比必须在 0.15 到 0.9 之间")
    if width <= 0 or depth <= 0 or wall <= 0 or wall * 2 >= min(width, depth):
        raise ValueError("多边形管截面尺寸或壁厚无效")

    shape_text = "正多边形管" if shape_mode == "regular" else "星形管"
    specification = f"{shape_text} · {_text(width)} × {_text(depth)} × {_text(wall)}"
    if shape_mode == "star":
        specification += f" · 星谷比 {_text(star_inner_ratio)}"
    return {
        "kind": "polygon",
        "shapeMode": shape_mode,
        "sideCount": side_count,
        "starInnerRatio": star_inner_ratio,
        "width": width,
        "depth": depth,
        "wallThickness": wall,
        "cornerRadius": 0.0,
        "specification": specification,
    }


def contours(
    profile: dict[str, Any], *, clearance: float = 0.0, swap_axes: bool = False,
) -> list[dict[str, Any]]:
    width = float(profile["width"])
    depth = float(profile["depth"])
    wall = float(profile["wallThickness"])
    shape_mode = str(profile["shapeMode"])
    side_count = int(profile["sideCount"])
    star_inner_ratio = float(profile["starInnerRatio"])
    outer_width = width + float(clearance) * 2
    outer_depth = depth + float(clearance) * 2
    if outer_width <= 0 or outer_depth <= 0:
        raise ValueError("多边形管偏置后的外轮廓无效")
    outer = _profile_points(
        outer_width,
        outer_depth,
        shape_mode,
        side_count,
        star_inner_ratio,
    )
    base_outer = _profile_points(
        width, depth, shape_mode, side_count, star_inner_ratio,
    )
    inner = _offset_inward(base_outer, wall)
    if (_signed_area(inner) <= 1.0e-8 or not _is_simple(inner)
            or not all(_contains_point(base_outer, point) for point in inner)):
        raise ValueError("壁厚过大或星谷半径比过小，无法形成有效内轮廓")

    def output(points: list[Point]) -> dict[str, Any]:
        coordinates = [[point[1], point[0]] if swap_axes else [point[0], point[1]]
                       for point in points]
        return {"kind": "polygon", "points": coordinates}

    return [output(outer), output(inner)]


def fitter(contours: list[dict[str, Any]], context: dict[str, Any] | None = None) -> dict[str, Any]:
    from profile_fitting import fitter_for_profile
    return fitter_for_profile(contours, "polygon", context)
