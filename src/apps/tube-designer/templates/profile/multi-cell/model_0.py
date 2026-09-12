"""矩形双腔管: 一个外环及两个独立内环；仅覆盖矩形双腔构造。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,s,x=p["width"],p["depth"],p["wallThickness"],p["ribThickness"],p["ribOffset"]
    a=w/2-t+x-s/2;b=w/2-t-x-s/2
    positive(a,b,h-2*t,t,s)
    return [box(w,h),box(a,h-2*t,-w/2+t+a/2),box(b,h-2*t,w/2-t-b/2)]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "rect-double-cell", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"矩形双腔管 {width:g} × {depth:g} mm"}
