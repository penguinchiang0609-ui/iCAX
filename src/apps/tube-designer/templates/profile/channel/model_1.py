"""无卷边 C 型钢: 此模板表示平行翼缘无卷边变体；并不以字母推断其他开口角。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h=p["width"],p["depth"]
    return [strip([[w/2,-h/2],[-w/2,-h/2],[-w/2,h/2],[w/2,h/2]],p["wallThickness"],p["bendRadius"])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "channel-cold-c", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"无卷边 C 型钢 {width:g} × {depth:g} mm"}
