"""正八边形管: 内外尺寸及内孔相位独立；仅覆盖正多边形变体。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    a,b=p["width"],p["innerSize"]
    positive(a-b,b)
    outer=regular(8,a)
    inner=regular(8,b,math.radians(p["innerPhase"]))
    return [polygon(outer,radii(p["outerRadii"],8)),polygon(inner,radii(p["innerRadii"],8))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "octagonal-tube", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"正八边形管 {width:g} × {depth:g} mm"}
