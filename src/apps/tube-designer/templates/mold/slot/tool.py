def generate(p, context):
    if p["spanAlong"] <= p["spanAcross"]:
        raise ValueError("腰形孔长度须大于宽度")
    return {"mode": "profile", "contours": [{"kind": "capsule", "width": p["spanAlong"], "height": p["spanAcross"]}]}
