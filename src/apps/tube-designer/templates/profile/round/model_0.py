"""圆管: 模板负责本截面的参数和构造；公共层不按管型名称分支。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    d,t=p["width"],p["wallThickness"]
    positive(d-2*t,t)
    return [{"kind":"circle","radius":d/2},{"kind":"circle","radius":d/2-t}]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "round", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"圆管 {width:g} × {depth:g} mm"}
