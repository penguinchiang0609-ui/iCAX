import math


def generate(p, context):
    radius, offset = p["diameter"] / 2, p["flatOffset"]
    if not 0 <= offset < radius:
        raise ValueError("单 D 孔须满足 0 ≤ 圆心到平边距离 < 基准圆半径")
    half = math.sqrt((radius - offset) * (radius + offset))
    return {"mode": "profile", "contours": [{"kind": "path", "segments": [
        {"kind": "arc", "start": [offset, half], "middle": [-radius, 0], "end": [offset, -half]},
        {"kind": "line", "start": [offset, -half], "end": [offset, half]}
    ]}]}
