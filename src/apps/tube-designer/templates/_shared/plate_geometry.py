"""Real plate solids and the shared manufacturing contract (all dimensions in mm)."""

from __future__ import annotations

import math
from typing import Any


def _positive(value: float, name: str) -> float:
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise ValueError(f"{name}必须是大于0的有限数值")
    return result


def plate_properties(width: float, height: float, thickness: float,
                     material_grade: str = "", category_key: str = "plate.infill",
                     category_name: str = "封板") -> dict[str, Any]:
    width = _positive(width, "板宽")
    height = _positive(height, "板高")
    thickness = _positive(thickness, "板厚")
    if thickness >= min(width, height):
        raise ValueError("板厚必须小于板宽和板高")
    result: dict[str, Any] = {
        "manufacturing.partKind": "plate",
        "manufacturing.materialCategory": "plate",
        "manufacturing.categoryKey": category_key,
        "manufacturing.categoryName": category_name,
        "manufacturing.plate": {"width": width, "height": height,
                                "thickness": thickness, "areaMm2": width * height},
        "length": max(width, height),
        "quantity": 1,
    }
    if material_grade and material_grade != "unspecified":
        result["manufacturing.material"] = material_grade
    return result


def emit_rectangular_plate(model: Any, key: str, *, width: float, height: float,
                           thickness: float, center=(0.0, 0.0, 0.0),
                           x_axis=(1.0, 0.0, 0.0), y_axis=(0.0, 0.0, 1.0),
                           holes=()) -> str:
    """Emit a centered solid plate; round holes use local center-relative x/y."""
    dimensions = plate_properties(width, height, thickness)["manufacturing.plate"]
    width, height, thickness = (dimensions[name] for name in ("width", "height", "thickness"))
    center, x_axis, y_axis = (tuple(float(v) for v in values)
                            for values in (center, x_axis, y_axis))
    if any(len(values) != 3 or not all(math.isfinite(v) for v in values)
           for values in (center, x_axis, y_axis)):
        raise ValueError("板件放置坐标无效")
    if (abs(sum(v * v for v in x_axis) - 1) > 1e-7
            or abs(sum(v * v for v in y_axis) - 1) > 1e-7
            or abs(sum(x_axis[i] * y_axis[i] for i in range(3))) > 1e-7):
        raise ValueError("板件局部坐标轴必须正交且为单位向量")
    normal = (x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1],
              x_axis[2] * y_axis[0] - x_axis[0] * y_axis[2],
              x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0])
    origin = [center[i] - normal[i] * thickness / 2 for i in range(3)]
    contour = model.geometry(f"{key}.outline", "profile2d", arguments={
        "placement": {"origin": origin, "xAxis": list(x_axis), "yAxis": list(y_axis)},
        "contours": [{"kind": "polygon", "points": [
            [-width / 2, -height / 2], [width / 2, -height / 2],
            [width / 2, height / 2], [-width / 2, height / 2]]}],
    })
    solid = model.geometry(f"{key}.solid", "extrude", inputs=[contour],
                           arguments={"vector": [v * thickness for v in normal]})
    cutters: list[str] = []
    for index, hole in enumerate(holes, start=1):
        x, y = float(hole["x"]), float(hole["y"])
        radius = _positive(hole["diameter"], "板孔直径") / 2
        if (not math.isfinite(x) or not math.isfinite(y)
                or abs(x) + radius >= width / 2 or abs(y) + radius >= height / 2):
            raise ValueError("板孔必须完整位于板件内并保留边缘材料")
        hole_origin = [origin[i] + x_axis[i] * x + y_axis[i] * y - normal[i]
                       for i in range(3)]
        profile = model.geometry(f"{key}.hole.{index}.profile", "profile2d", arguments={
            "placement": {"origin": hole_origin, "xAxis": list(x_axis), "yAxis": list(y_axis)},
            "contours": [{"kind": "circle", "radius": radius}],
        })
        cutters.append(model.geometry(f"{key}.hole.{index}.solid", "extrude", inputs=[profile],
                                      arguments={"vector": [v * (thickness + 2) for v in normal]}))
    if cutters:
        return model.geometry(f"{key}.finished", "boolean", inputs=[solid, *cutters],
                              arguments={"operation": "subtract", "target": solid, "tools": cutters})
    return solid
