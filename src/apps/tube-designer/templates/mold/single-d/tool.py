def generate(p, context):
    width = p["spanAlong"]
    height = p["spanAcross"]
    if width < height:
        raise ValueError("单 D 孔总长度须不小于圆弧直径")
    half = height / 2
    left = -width / 2
    arc_center = width / 2 - half
    return {"mode": "profile", "contours": [{"kind": "path", "segments": [
        {"kind": "line", "start": [left, -half], "end": [arc_center, -half]},
        {"kind": "arc", "start": [arc_center, -half], "middle": [width / 2, 0],
         "end": [arc_center, half]},
        {"kind": "line", "start": [arc_center, half], "end": [left, half]},
        {"kind": "line", "start": [left, half], "end": [left, -half]}
    ]}]}
