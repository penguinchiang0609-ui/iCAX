"""真椭圆管: 内外均为真实椭圆，内轴独立；不宣称是等距曲线。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t=p["width"],p["depth"],p["wallThickness"]
    a=p["innerWidth"] or w-2*t;b=p["innerDepth"] or h-2*t
    positive(a,b,w-a,h-b,t)
    return [{"kind":"ellipse","width":w,"height":h},{"kind":"ellipse","width":a,"height":b}]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "ellipse", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"真椭圆管 {width:g} × {depth:g} mm"}
