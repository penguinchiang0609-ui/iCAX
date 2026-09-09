def generate(p, context):
    if p["spanAlong"] <= p["spanAcross"]:
        raise ValueError("双 D 孔沿长度尺寸须大于横向尺寸")
    return {"mode": "profile", "contours": [{"kind": "capsule", "width": p["spanAlong"],
                                                  "height": p["spanAcross"]}]}
