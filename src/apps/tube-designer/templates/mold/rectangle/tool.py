import math


def rounded_rectangle_path(width, height, radius):
    if radius <= 0:
        points=[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]]
        return {"kind":"path", "closed":True, "segments":[
            {"kind":"line","start":points[i],"end":points[(i+1)%4]} for i in range(4)
        ]}
    q=radius/math.sqrt(2.0); x=width/2; y=height/2
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line","start":[-x+radius,-y],"end":[x-radius,-y]},
        {"kind":"arc","start":[x-radius,-y],"middle":[x-radius+q,-y+radius-q],"end":[x,-y+radius]},
        {"kind":"line","start":[x,-y+radius],"end":[x,y-radius]},
        {"kind":"arc","start":[x,y-radius],"middle":[x-radius+q,y-radius+q],"end":[x-radius,y]},
        {"kind":"line","start":[x-radius,y],"end":[-x+radius,y]},
        {"kind":"arc","start":[-x+radius,y],"middle":[-x+radius-q,y-radius+q],"end":[-x,y-radius]},
        {"kind":"line","start":[-x,y-radius],"end":[-x,-y+radius]},
        {"kind":"arc","start":[-x,-y+radius],"middle":[-x+radius-q,-y+radius-q],"end":[-x+radius,-y]},
    ]}


def generate(p, context):
    if p["cornerRadius"] >= min(p["spanAlong"], p["spanAcross"]) / 2:
        raise ValueError("圆角半径不能超过短边的一半")
    return {"mode": "profile", "contours": [rounded_rectangle_path(p["spanAlong"], p["spanAcross"], p["cornerRadius"])]}
