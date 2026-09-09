import math


def generate(p, context):
    across_flats = p["acrossFlats"]
    half_flat = across_flats / 2
    radius = across_flats / math.sqrt(3)
    points = [
        [radius, 0], [radius / 2, half_flat], [-radius / 2, half_flat],
        [-radius, 0], [-radius / 2, -half_flat], [radius / 2, -half_flat]
    ]
    return {"mode": "profile", "contours": [{"kind": "polygon", "points": points}]}
