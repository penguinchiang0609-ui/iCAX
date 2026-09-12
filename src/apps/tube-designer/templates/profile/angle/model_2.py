"""精密尖角角钢: 精密实心截面，不采用薄板冷弯半径规则。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,f=p["width"],p["depth"],p["wallThickness"],p["flangeThickness"]
    positive(w-t,h-f)
    pts=[[-w/2,-h/2],[w/2,-h/2],[w/2,-h/2+f],[-w/2+t,-h/2+f],[-w/2+t,h/2],[-w/2,h/2]]
    return [polygon(pts)]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "angle-precision", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"精密尖角角钢 {width:g} × {depth:g} mm"}
