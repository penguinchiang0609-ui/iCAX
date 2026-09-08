def generate(p, context):
    return {"mode": "profile", "contours": [{"kind": "circle", "radius": p["diameter"] / 2}]}
