"""圆棒 / 圆钢: 按实心截面生成。"""
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
    return [circle_path([0, 0], p["width"] / 2)]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "round-bar", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"圆棒 / 圆钢 {width:g} × {depth:g} mm"}
