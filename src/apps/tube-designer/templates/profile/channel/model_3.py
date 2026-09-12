"""内卷边 C 型钢: 模板负责本截面的参数和构造；公共层不按管型名称分支。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h=p["width"],p["depth"]
    return [strip([[w/2,-h/2+p["lipLength"]],[w/2,-h/2],[-w/2,-h/2],[-w/2,h/2],[w/2,h/2],[w/2,h/2-p["lipLength"]]],p["wallThickness"],p["bendRadius"])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "channel-lipped-inward", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"内卷边 C 型钢 {width:g} × {depth:g} mm"}
