import math


def capsule_path(width, height):
    radius = height / 2
    center = (width - height) / 2
    if width <= height:
        raise ValueError("腰形孔长度须大于宽度")
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line", "start":[-center,-radius], "end":[center,-radius]},
        {"kind":"arc", "start":[center,-radius], "middle":[center+radius,0], "end":[center,radius]},
        {"kind":"line", "start":[center,radius], "end":[-center,radius]},
        {"kind":"arc", "start":[-center,radius], "middle":[-center-radius,0], "end":[-center,-radius]},
    ]}


def generate(p, context):
    if p["spanAlong"] <= p["spanAcross"]:
        raise ValueError("腰形孔长度须大于宽度")
    return {"mode": "profile", "contours": [capsule_path(p["spanAlong"], p["spanAcross"])]}
