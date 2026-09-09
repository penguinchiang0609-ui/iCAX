def generate(p, context):
    size = p["size"]
    half = size / 2
    return {"mode": "profile", "contours": [{"kind": "polygon", "points": [
        [-half, -half], [half, -half], [half, half], [-half, half]
    ]}]}
