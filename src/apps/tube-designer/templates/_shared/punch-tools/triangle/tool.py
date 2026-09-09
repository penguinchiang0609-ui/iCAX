def generate(p, context):
    half = p["spanAlong"] / 2
    height = p["spanAcross"]
    return {"mode": "profile", "contours": [{"kind": "polygon", "points": [
        [-half, -height / 2], [half, -height / 2], [0, height / 2]
    ]}]}
