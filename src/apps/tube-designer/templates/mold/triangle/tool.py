def path(points):
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line", "start":list(points[i]), "end":list(points[(i+1)%len(points)])}
        for i in range(len(points))
    ]}


def generate(p, context):
    half = p["spanAlong"] / 2
    height = p["spanAcross"]
    return {"mode": "profile", "contours": [path([
        [-half, -height / 2], [half, -height / 2], [0, height / 2]
    ])]}
