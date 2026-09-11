def generate(p, context):
    if p["cornerRadius"] >= min(p["spanAlong"], p["spanAcross"]) / 2:
        raise ValueError("圆角半径不能超过短边的一半")
    return {"mode": "profile", "contours": [{"kind": "roundedRectangle", "width": p["spanAlong"], "height": p["spanAcross"], "radius": p["cornerRadius"]}]}
