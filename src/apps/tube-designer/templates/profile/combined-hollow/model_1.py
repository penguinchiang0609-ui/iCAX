"""外六角内圆管: 模板负责本截面的参数和构造；公共层不按管型名称分支。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    positive(p["width"]-p["innerDiameter"])
    return [polygon(regular(6,p["width"])),{"kind":"circle","radius":p["innerDiameter"]/2}]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "hex-round-bore", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"外六角内圆管 {width:g} × {depth:g} mm"}
