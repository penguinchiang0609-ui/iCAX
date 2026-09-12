"""D 型管（半圆闭口变体）: 仅为带平底的闭口半圆 D 变体，不代表所有市场 D 型。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    r,t=p["width"],p["wallThickness"]
    positive(r-2*t,t)
    c=-r/2;ri=r-t;y=math.sqrt(ri*ri-t*t)
    def loop(x,y,right):return {"kind":"path","closed":True,"segments":[{"kind":"line","start":[x,-y],"end":[x,y]},{"kind":"arc","start":[x,y],"middle":[right,0],"end":[x,-y]}]}
    return [loop(c,r,c+r),loop(c+t,y,c+ri)]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "d-semicircular", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"D 型管（半圆闭口变体） {width:g} × {depth:g} mm"}
