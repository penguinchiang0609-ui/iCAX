from __future__ import annotations

import math
from typing import Any


def _number(parameters: dict[str, Any], key: str) -> float:
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} 必须是数值")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{key} 必须是有限数值")
    return result


def _text(value: float) -> str:
    return f"{value:.6f}".rstrip("0").rstrip(".")


def build(parameters: dict[str, Any]) -> dict[str, Any]:
    width = _number(parameters, "width")
    depth = _number(parameters, "depth")
    thickness = _number(parameters, "wallThickness")
    if width <= 0 or depth <= 0 or thickness <= 0 or thickness >= min(width, depth):
        raise ValueError("L 型角钢截面尺寸或厚度无效")
    return {
        "kind": "angle",
        "width": width,
        "depth": depth,
        "wallThickness": thickness,
        "cornerRadius": 0.0,
        "specification": f"L{_text(width)} × {_text(depth)} × {_text(thickness)}",
    }


def contours(
    profile: dict[str, Any], *, clearance: float = 0.0, swap_axes: bool = False,
) -> list[dict[str, Any]]:
    clearance = float(clearance)
    width = float(profile["width"]) + clearance * 2
    depth = float(profile["depth"]) + clearance * 2
    thickness = float(profile["wallThickness"]) + clearance
    if width <= 0 or depth <= 0 or thickness <= 0 or thickness >= min(width, depth):
        raise ValueError("L 型角钢偏置后的二维轮廓无效")
    half_width, half_depth = width / 2, depth / 2
    points = [
        [-half_width, -half_depth], [half_width, -half_depth],
        [half_width, -half_depth + thickness],
        [-half_width + thickness, -half_depth + thickness],
        [-half_width + thickness, half_depth], [-half_width, half_depth],
    ]
    if swap_axes:
        points = [[y, x] for x, y in points]
    return [{"kind": "polygon", "points": points}]


def fitter(contours: list[dict[str, Any]], context: dict[str, Any] | None = None) -> dict[str, Any]:
    from profile_fitting import fitter_for_profile
    return fitter_for_profile(contours, "angle", context)
