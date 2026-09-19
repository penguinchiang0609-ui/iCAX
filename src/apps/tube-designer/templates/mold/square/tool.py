def path(points):
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line", "start":list(points[i]), "end":list(points[(i+1)%len(points)])}
        for i in range(len(points))
    ]}


def generate(p, context):
    size = p["size"]
    half = size / 2
    return {"mode": "profile", "contours": [path([
        [-half, -half], [half, -half], [half, half], [-half, half]
    ])]}
