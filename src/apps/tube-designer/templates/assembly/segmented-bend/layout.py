"""Direct measurements of this template's inherited default slot pattern.

Only numeric defaults and geometric relations are read here. No cutter,
finished mesh, candidate section, or manufacturing plan is generated.
"""
import json
import math
from pathlib import Path

_tool = json.loads((Path(__file__).resolve().parents[2] / "mold" / "segmented-bend" /
                    "tool.json").read_text(encoding="utf-8"))
_defaults = {item["key"]: item["defaultValue"] for item in _tool["parameters"]}


def calculate(depth, wall, bend_radius, angle):
    """Use the same radius, pitch, root and allowance relations as tool.py."""
    p = _defaults
    half_depth = depth / 2
    height = depth - wall - p["rootClearance"]
    radius = bend_radius + (half_depth if p["radiusDatum"] == "inner" else 0)
    if height <= 0 or radius <= half_depth:
        raise ValueError("槽根超出顶部或中心线半径不大于截面半高")
    beta = math.radians(angle)
    if not 0 < beta < math.pi:
        raise ValueError("总折弯角须介于 0 与 180°")
    count = p["segmentCount"]
    spacing = p["spacingModel"]
    if p["countMode"] == "tolerance":
        error = p["maximumChordError"]
        step = (4 * math.asin(math.sqrt(min(1, error / (2 * radius))))
                if spacing == "chord" else 2 * math.acos(1 / (1 + error / radius)))
        if step <= 0:
            raise ValueError("弓高误差过小")
        count = max(2, math.ceil(beta / step))
    if type(count) not in (int, float) or int(count) != count or not 2 <= count <= 64:
        raise ValueError("分段槽数必须为 2 至 64；当前误差要求可能过小")
    delta = beta / int(count)
    pitch = (2 * radius * math.sin(delta / 2) if spacing == "chord"
             else 2 * radius * math.tan(delta / 2))
    allowance = p["kFactor"] * wall * delta if p["bendCompensation"] else 0
    opening = 2 * height * math.tan(delta / 2) + allowance
    remaining = pitch - opening
    if remaining < p["minimumLand"]:
        raise ValueError("相邻槽的剩余间隔不足，请增大半径、槽数或减小补偿")
    return {"patternLength": (int(count) - 1) * pitch + opening,
            "remainingLand": remaining}
