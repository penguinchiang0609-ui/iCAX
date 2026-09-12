"""平行四边形管: 模板负责本截面的参数和构造；公共层不按管型名称分支。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,s,t=p["width"],p["depth"],p["shear"],p["wallThickness"]
    a=[[-w/2-s/2,-h/2],[w/2-s/2,-h/2],[w/2+s/2,h/2],[-w/2+s/2,h/2]]
    return [polygon(a),polygon(offset(a,t))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "parallelogram-tube", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"平行四边形管 {width:g} × {depth:g} mm"}
