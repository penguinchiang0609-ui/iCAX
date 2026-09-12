"""剖分 T 型钢: 保留 H 型钢翼缘与根部圆角；不冒充独立船用热轧 T 系列。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,f,x=p["width"],p["depth"],p["wallThickness"],p["flangeThickness"],p["webOffset"]
    positive(w-t-2*abs(x),h-f)
    pts=[[-w/2,h/2-f],[x-t/2,h/2-f],[x-t/2,-h/2],[x+t/2,-h/2],[x+t/2,h/2-f],[w/2,h/2-f],[w/2,h/2],[-w/2,h/2]]
    return [polygon(pts,[p["toeRadius"],p["rootRadius"],0,0,p["rootRadius"],p["toeRadius"],0,0])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "t-split", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"剖分 T 型钢 {width:g} × {depth:g} mm"}
