import math


def circle_path(radius):
    k = radius / math.sqrt(2.0)
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"arc","start":[radius,0],"middle":[k,k],"end":[0,radius]},
        {"kind":"arc","start":[0,radius],"middle":[-k,k],"end":[-radius,0]},
        {"kind":"arc","start":[-radius,0],"middle":[-k,-k],"end":[0,-radius]},
        {"kind":"arc","start":[0,-radius],"middle":[k,-k],"end":[radius,0]},
    ]}


def generate(p, context):
    return {"mode": "profile", "contours": [circle_path(p["diameter"] / 2)]}
