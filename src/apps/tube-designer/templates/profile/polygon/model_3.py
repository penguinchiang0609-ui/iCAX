"""直角梯形管: 直边等厚变体；其他偏心内孔和非等厚构造需按具体截面补充。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,a,t=p["width"],p["depth"],p["topWidth"],p["wallThickness"]
    positive(w-a,a,h,t)
    x=-w/2+a/2
    pts=[[-w/2,-h/2],[w/2,-h/2],[x+a/2,h/2],[x-a/2,h/2]]
    return [polygon(pts),polygon(offset(pts,t))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "trapezoid-right", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"直角梯形管 {width:g} × {depth:g} mm"}
