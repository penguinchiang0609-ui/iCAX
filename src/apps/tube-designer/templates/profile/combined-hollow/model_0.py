"""外圆内六角管: 模板负责本截面的参数和构造；公共层不按管型名称分支。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    positive(p["width"]-p["innerSize"]/math.cos(math.pi/6))
    return [{"kind":"circle","radius":p["width"]/2},polygon(regular(6,p["innerSize"],math.radians(p["innerPhase"])))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "round-hex-bore", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"外圆内六角管 {width:g} × {depth:g} mm"}
