import math


def generate(p, context):
    radius, half, distance = p["headDiameter"] / 2, p["neckWidth"] / 2, p["centerDistance"]
    if not 0 < half < radius:
        raise ValueError("钥匙孔颈部宽度须大于 0 且小于大圆直径")
    join = math.sqrt((radius - half) * (radius + half))
    if distance <= join:
        raise ValueError("钥匙孔颈端圆心须位于大圆与颈部交线之外")
    return {"mode": "profile", "contours": [{"kind": "path", "segments": [
        {"kind": "line", "start": [join, -half], "end": [distance, -half]},
        {"kind": "arc", "start": [distance, -half], "middle": [distance + half, 0], "end": [distance, half]},
        {"kind": "line", "start": [distance, half], "end": [join, half]},
        {"kind": "arc", "start": [join, half], "middle": [-radius, 0], "end": [join, -half]}
    ]}]}
