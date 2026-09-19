import math


def generate(p, context):
    across_flats = p["acrossFlats"]
    half_flat = across_flats / 2
    radius = across_flats / math.sqrt(3)
    points = [
        [radius, 0], [radius / 2, half_flat], [-radius / 2, half_flat],
        [-radius, 0], [-radius / 2, -half_flat], [radius / 2, -half_flat]
    ]
    return {"mode": "profile", "contours": [path(points)]}
def path(points):
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line", "start":list(points[i]), "end":list(points[(i+1)%len(points)])}
        for i in range(len(points))
    ]}

