"""开口梯形等厚U肋；尺寸为外侧理论交点尺寸，端面齐平于开口高度。"""
import math
from .geometry import positive, polygon, swap, envelope

def shape(p):
    w,b,h,t,r=[p[k] for k in ("width","bottomWidth","depth","wallThickness","bendRadius")]
    if not all(math.isfinite(v) for v in (w,b,h,t,r)):
        raise ValueError("U肋参数必须为有限数值")
    positive(w,b,h,t)
    if w<b: raise ValueError("U肋开口外宽不能小于底部理论外宽")
    if r<0: raise ValueError("内弯半径不能为负数")
    positive(h-t)
    slope=(w-b)/(2*h)
    # Offset the sloping lines by t along their normals, not by t along X.
    inset=t*math.hypot(1,slope)
    inner_top=w/2-inset
    inner_bottom=b/2+slope*t-inset
    positive(inner_top,inner_bottom)
    points=[[-w/2,h],[-b/2,0],[b/2,0],[w/2,h],
            [inner_top,h],[inner_bottom,t],[-inner_bottom,t],[-inner_top,h]]
    return [polygon(points,[0,r+t,r+t,0,0,r,r,0])]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "u-section", "width": width, "depth": depth,
            "wallThickness": p["wallThickness"], "cornerRadius": p["bendRadius"],
            "specification": f"U肋 {p['width']:g}/{p['bottomWidth']:g} × {p['depth']:g} × {p['wallThickness']:g} mm"}
