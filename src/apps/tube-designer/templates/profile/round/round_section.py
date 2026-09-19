"""圆管: 模板负责本截面的参数和构造；公共层不按管型名称分支。"""
import math
from .profile import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def circle_path(center, radius):
    positive(radius)
    cx, cy = center
    points = [[cx + radius * math.cos(math.tau * i / 4),
               cy + radius * math.sin(math.tau * i / 4)] for i in range(4)]
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "arc", "start": points[i],
         "middle": [cx + radius * math.cos(math.tau * (i + 0.5) / 4),
                     cy + radius * math.sin(math.tau * (i + 0.5) / 4)],
         "end": points[(i + 1) % 4]}
        for i in range(4)
    ]}

def shape(p):
    d,t=p["width"],p["wallThickness"]
    dx,dy=p["innerOffsetX"],p["innerOffsetY"]
    outer_radius=d/2
    inner_radius=outer_radius-t
    positive(inner_radius,t)
    if math.hypot(dx,dy)+inner_radius >= outer_radius:
        raise ValueError("内孔偏心导致内外圆相交")
    return [circle_path([0, 0], outer_radius),
            circle_path([dx, dy], inner_radius)]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "round", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"圆管 {width:g} × {depth:g} mm"}
