"""Declare frozen component-model references without guessing their geometry.

Product code resolves these references to real files and replaces each resource
node with a BREP before the generic geometry evaluator sees the model. Template
code only gives rigid placement, provenance and purchasing/manufacturing intent.
"""
from __future__ import annotations

import hashlib
import math
import re
from typing import Any


def _reference(value: str) -> str:
    if not isinstance(value, str) or re.fullmatch(r"(?:system|template|library):[A-Za-z0-9][A-Za-z0-9._-]*", value) is None:
        raise ValueError("配件模型引用应为 system:模型ID、template:资源名 或 library:模型ID")
    return value


def component_properties(reference: str, *, sourcing: str = "purchased",
                         category_key: str = "accessory.component", category_name: str = "配件") -> dict[str, Any]:
    if sourcing not in {"made", "purchased"}:
        raise ValueError("配件来源只能是自制或外购")
    return {
        "manufacturing.partKind": "accessory",
        "manufacturing.materialCategory": "accessory",
        "manufacturing.modelReference": _reference(reference),
        "manufacturing.sourcing": sourcing,
        "manufacturing.process": "purchased" if sourcing == "purchased" else "machined",
        "manufacturing.categoryKey": category_key,
        "manufacturing.categoryName": category_name,
        "quantity": 1,
    }


class ComponentModelGeometry:
    """One immutable resource declaration, independent rigid placement per item."""

    def __init__(self, model: Any):
        self.model = model
        self.resources: dict[str, str] = {}

    def emit(self, key: str, reference: str, *, origin=(0.0, 0.0, 0.0),
             x_axis=(1.0, 0.0, 0.0), y_axis=(0.0, 1.0, 0.0)) -> str:
        reference = _reference(reference)
        axes = [tuple(float(v) for v in value) for value in (origin, x_axis, y_axis)]
        if any(len(value) != 3 or not all(math.isfinite(v) for v in value) for value in axes):
            raise ValueError("配件放置坐标必须包含3个有限数值")
        origin, x_axis, y_axis = axes
        if (abs(sum(v * v for v in x_axis) - 1) > 1e-7
                or abs(sum(v * v for v in y_axis) - 1) > 1e-7
                or abs(sum(x_axis[i] * y_axis[i] for i in range(3))) > 1e-7):
            raise ValueError("配件只能作刚体放置，不支持非均匀缩放或斜切坐标轴")
        z_axis = (x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1],
                  x_axis[2] * y_axis[0] - x_axis[0] * y_axis[2],
                  x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0])
        if reference not in self.resources:
            resource_key = "shared.component." + hashlib.sha256(reference.encode("utf-8")).hexdigest()[:24]
            self.resources[reference] = self.model.geometry(resource_key, "resource", arguments={"reference": reference})
        return self.model.geometry(key + ".placed", "transform", inputs=[self.resources[reference]],
                                   arguments={"placement": {"origin": list(origin), "xAxis": list(x_axis),
                                                            "yAxis": list(y_axis), "zAxis": list(z_axis)}})
