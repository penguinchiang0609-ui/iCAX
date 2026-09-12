"""不等边不等厚角钢: 圆角及局部增厚为独立输入；未录入标准系列尺寸表。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w=p["width"];h=p["depth"];t=p["wallThickness"];f=p["flangeThickness"]
    positive(w-t,h-f)
    pts=[[-w/2,-h/2],[w/2,-h/2],[w/2,-h/2+f],[-w/2+t,-h/2+f],[-w/2+t,h/2],[-w/2,h/2]]
    return [polygon(pts,[p["heelRadius"],p["toeRadius"],p["toeRadius"],p["rootRadius"],p["toeRadius"],p["toeRadius"]])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "angle-hot-unequal-thickness", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"不等边不等厚角钢 {width:g} × {depth:g} mm"}
