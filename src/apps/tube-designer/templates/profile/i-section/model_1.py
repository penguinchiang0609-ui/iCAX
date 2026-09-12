"""热轧斜翼缘工字钢: 参数化几何族；数值为演示尺寸，不宣称对应某一国标牌号/系列。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    w,h,t,f=p["width"],p["depth"],p["wallThickness"],p["flangeThickness"]
    s=math.tan(math.radians(p["flangeSlope"]))
    y=-h/2+f+(w-t)/2*s
    positive(w-t,-2*y)
    pts=[[-w/2,-h/2],[w/2,-h/2],[w/2,-h/2+f],[t/2,y],[t/2,-y],[w/2,h/2-f],[w/2,h/2],[-w/2,h/2],[-w/2,h/2-f],[-t/2,-y],[-t/2,y],[-w/2,-h/2+f]]
    roots={3,4,9,10};toes={2,5,8,11}
    return [polygon(pts,[p["rootRadius"] if i in roots else p["toeRadius"] if i in toes else 0 for i in range(12)])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "i-hot-tapered", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"热轧斜翼缘工字钢 {width:g} × {depth:g} mm"}
