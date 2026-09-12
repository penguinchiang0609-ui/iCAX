"""焊接 H 型钢: 上下翼缘宽厚及腹板偏心独立；焊脚是名义包络。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,f,b,wb,x=p["width"],p["depth"],p["wallThickness"],p["flangeThickness"],p["lowerThickness"],p["lowerWidth"],p["webOffset"]
    positive(min(w,wb)-t-2*abs(x),h-f-b)
    pts=[[-wb/2,-h/2],[wb/2,-h/2],[wb/2,-h/2+b],[x+t/2,-h/2+b],[x+t/2,h/2-f],[w/2,h/2-f],[w/2,h/2],[-w/2,h/2],[-w/2,h/2-f],[x-t/2,h/2-f],[x-t/2,-h/2+b],[-wb/2,-h/2+b]]
    return [polygon(chamfer(pts,{i:p["weldLeg"] for i in (3,4,9,10)}))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "h-welded", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"焊接 H 型钢 {width:g} × {depth:g} mm"}
