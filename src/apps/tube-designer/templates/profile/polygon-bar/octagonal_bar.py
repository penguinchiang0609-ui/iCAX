"""八角棒: 按实心截面生成。"""
import math
from .profile import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    return [polygon(regular(8,p["width"]))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "octagonal-bar", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"八角棒 {width:g} × {depth:g} mm"}
