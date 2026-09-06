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
    wall = _number(parameters, "wallThickness")
    if width <= 0 or depth <= 0 or wall <= 0 or wall * 2 >= min(width, depth):
        raise ValueError("腰圆管截面尺寸或壁厚无效")
    return {
        "kind": "flat-oval",
        "width": width,
        "depth": depth,
        "wallThickness": wall,
        "cornerRadius": 0.0,
        "specification": f"{_text(width)} × {_text(depth)} × {_text(wall)}",
    }


def contours(
    profile: dict[str, Any], *, clearance: float = 0.0, swap_axes: bool = False,
) -> list[dict[str, Any]]:
    wall = float(profile["wallThickness"])
    sizes = [
        (float(profile["width"]) + float(clearance) * 2,
         float(profile["depth"]) + float(clearance) * 2),
        (float(profile["width"]) - wall * 2, float(profile["depth"]) - wall * 2),
    ]
    result: list[dict[str, Any]] = []
    for width, depth in sizes:
        if width <= 0 or depth <= 0:
            raise ValueError("腰圆管偏置后的二维轮廓无效")
        if math.isclose(width, depth, rel_tol=0.0, abs_tol=1e-9):
            result.append({"kind": "circle", "radius": width / 2})
            continue
        result.append({
            "kind": "capsule",
            "width": depth if swap_axes else width,
            "height": width if swap_axes else depth,
        })
    return result
