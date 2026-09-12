"""跑道圆 / 长圆管: 仅表示两直线加两端半圆；不冒充平椭圆或所有腰型管。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t=p["width"],p["depth"],p["wallThickness"]
    positive(w-2*t,h-2*t,t)
    return [{"kind":"circle","radius":a/2} if abs(a-b)<1e-9 else {"kind":"capsule","width":a,"height":b} for a,b in [(w,h),(w-2*t,h-2*t)]]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "racetrack", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"跑道圆 / 长圆管 {width:g} × {depth:g} mm"}
