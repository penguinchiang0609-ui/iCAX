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
    diameter = _number(parameters, "width")
    wall = _number(parameters, "wallThickness")
    if diameter <= 0 or wall <= 0 or wall * 2 >= diameter:
        raise ValueError("圆管外径或壁厚无效")
    return {
        "kind": "round",
        "width": diameter,
        "depth": diameter,
        "wallThickness": wall,
        "cornerRadius": 0.0,
        "specification": f"Φ{_text(diameter)} × {_text(wall)}",
    }


def contours(
    profile: dict[str, Any], *, clearance: float = 0.0, swap_axes: bool = False,
) -> list[dict[str, Any]]:
    del swap_axes
    outer_radius = float(profile["width"]) / 2 + float(clearance)
    inner_radius = float(profile["width"]) / 2 - float(profile["wallThickness"])
    if outer_radius <= 0 or inner_radius <= 0 or inner_radius >= outer_radius:
        raise ValueError("圆管偏置后的二维轮廓无效")
    return [
        {"kind": "circle", "radius": outer_radius},
        {"kind": "circle", "radius": inner_radius},
    ]


def fitter(contours: list[dict[str, Any]], context: dict[str, Any] | None = None) -> dict[str, Any]:
    from profile_fitting import fitter_for_profile
    return fitter_for_profile(contours, "round", context)
