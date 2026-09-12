"""圆角矩形管: 内孔尺寸、偏心和内外四角可独立指定；默认尺寸仅为可编辑示例，不是国标规格。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope
from .geometry import corner_polygon

def shape(p):
    w=p["width"];h=p["depth"];t=p["wallThickness"]
    iw=p["innerWidth"] or w-2*t;ih=p["innerDepth"] or h-2*t
    x,y=p["innerOffsetX"],p["innerOffsetY"]
    positive(iw,ih,w-iw-2*abs(x),h-ih-2*abs(y),t)
    outer=radii(p["outerRadii"],4,p["cornerRadius"])
    inner=radii(p["innerRadii"],4,max(0,p["cornerRadius"]-t))
    def corners(a,b,cx,cy,rs,key):
        pts=[[cx-a/2,cy-b/2],[cx+a/2,cy-b/2],[cx+a/2,cy+b/2],[cx-a/2,cy+b/2]]
        return corner_polygon(pts,rs,p.get(key,""))
    return [corners(w,h,0,0,outer,"outerCorners"),corners(iw,ih,x,y,inner,"innerCorners")]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "rect", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"圆角矩形管 {width:g} × {depth:g} mm"}
