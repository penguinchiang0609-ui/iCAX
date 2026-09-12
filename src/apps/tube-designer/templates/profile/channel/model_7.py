"""板焊槽钢: 三块板厚独立；根部用名义焊脚直线包络，不套热轧圆角。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,f,b=p["width"],p["depth"],p["wallThickness"],p["flangeThickness"],p["lowerThickness"]
    positive(w-t,h-f-b)
    pts=[[-w/2,-h/2],[w/2,-h/2],[w/2,-h/2+b],[-w/2+t,-h/2+b],[-w/2+t,h/2-f],[w/2,h/2-f],[w/2,h/2],[-w/2,h/2]]
    return [polygon(chamfer(pts,{3:p["weldLeg"],4:p["weldLeg"]}))]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "channel-welded", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"板焊槽钢 {width:g} × {depth:g} mm"}
