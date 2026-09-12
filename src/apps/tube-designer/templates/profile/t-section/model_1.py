"""焊接 T 型钢: 焊接根部与热轧根圆角分开。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,f,x=p["width"],p["depth"],p["wallThickness"],p["flangeThickness"],p["webOffset"]
    positive(w-t-2*abs(x),h-f)
    pts=[[-w/2,h/2-f],[x-t/2,h/2-f],[x-t/2,-h/2],[x+t/2,-h/2],[x+t/2,h/2-f],[w/2,h/2-f],[w/2,h/2],[-w/2,h/2]]
    return [polygon(chamfer(pts,{1:p["weldLeg"],4:p["weldLeg"]}))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "t-welded", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"焊接 T 型钢 {width:g} × {depth:g} mm"}
