"""冷弯帽型 / Ω 型钢: width 为两侧底部中心线间距，crownWidth 控制斜腹板，不是热轧帽型。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,a,f=p["width"],p["depth"],p["crownWidth"],p["flangeWidth"]
    if a > w: raise ValueError("帽顶宽度不能超过腹板底部间距")
    positive(w,a,h,f)
    return [strip([[-w/2-f,-h/2],[-w/2,-h/2],[-a/2,h/2],[a/2,h/2],[w/2,-h/2],[w/2+f,-h/2]],p["wallThickness"],p["bendRadius"])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "omega-cold", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"冷弯帽型 / Ω 型钢 {width:g} × {depth:g} mm"}
