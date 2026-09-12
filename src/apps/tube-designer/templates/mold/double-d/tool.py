import math


def generate(p, context):
    radius, half = p["diameter"] / 2, p["flatSpacing"] / 2
    if not 0 < half < radius:
        raise ValueError("双 D 孔两平边间距须大于 0 且小于基准圆直径")
    x = math.sqrt((radius - half) * (radius + half))
    return {"mode": "profile", "contours": [{"kind": "path", "segments": [
        {"kind": "line", "start": [-x, -half], "end": [x, -half]},
        {"kind": "arc", "start": [x, -half], "middle": [radius, 0], "end": [x, half]},
        {"kind": "line", "start": [x, half], "end": [-x, half]},
        {"kind": "arc", "start": [-x, half], "middle": [-radius, 0], "end": [-x, -half]}
    ]}]}
