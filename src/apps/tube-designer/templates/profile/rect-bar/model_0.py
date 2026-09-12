"""扁钢 / 矩形棒: 按实心截面生成。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    return [box(p["width"],p["depth"],rs=[p["cornerRadius"]]*4)]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "flat-bar", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"扁钢 / 矩形棒 {width:g} × {depth:g} mm"}
