def generate(p, context):
    return {"mode": "profile", "contours": [{"kind": "ellipse", "width": p["spanAlong"], "height": p["spanAcross"]}]}
