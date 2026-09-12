"""冷弯不等边角钢: 两肢中心线长度独立，成品包络由板厚和圆角计算。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h=p["width"],p["depth"]
    return [strip([[-w/2,h/2],[-w/2,-h/2],[w/2,-h/2]],p["wallThickness"],p["bendRadius"])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "angle-cold-unequal", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"冷弯不等边角钢 {width:g} × {depth:g} mm"}
