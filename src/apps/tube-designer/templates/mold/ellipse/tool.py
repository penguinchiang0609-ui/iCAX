import math


def ellipse_path(width, height):
    major, minor = max(width, height)/2, min(width, height)/2
    rotation = 0.0 if width >= height else math.pi/2
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"ellipseArc", "center":[0,0], "majorRadius":major, "minorRadius":minor,
         "rotation":rotation, "startAngle":a, "endAngle":a+math.pi/2}
        for a in (0.0, math.pi/2, math.pi, 3*math.pi/2)
    ]}


def generate(p, context):
    return {"mode": "profile", "contours": [ellipse_path(p["spanAlong"], p["spanAcross"])]}
