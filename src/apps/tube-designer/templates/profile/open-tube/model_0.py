"""纵向开缝圆管: 实体边界仍闭合，但只有一个边界环；缝隙不被虚构为封闭孔。"""
import math
from .geometry import positive, radii, polygon, box, regular, offset, strip, chamfer, swap, envelope

def shape(p):
    r,t,a=p["width"]/2,p["wallThickness"],math.radians(p["openingAngle"])/2
    positive(r-t)
    if a>=math.pi:raise ValueError("开口角必须小于360度")
    ri=r-t
    outer0=[r*math.cos(a),r*math.sin(a)];outer1=[r*math.cos(a),-r*math.sin(a)]
    inner0=[ri*math.cos(a),ri*math.sin(a)];inner1=[ri*math.cos(a),-ri*math.sin(a)]
    return [{"kind":"path","closed":True,"segments":[{"kind":"arc","start":outer0,"middle":[-r,0],"end":outer1},{"kind":"line","start":outer1,"end":inner1},{"kind":"arc","start":inner1,"middle":[-ri,0],"end":inner0},{"kind":"line","start":inner0,"end":outer0}]}]

def build(parameters):
    p = dict(parameters)
    contours = shape(p)
    width, depth = envelope(contours)
    positive(width, depth)
    return {"contours": contours, **p, "_parameters": p, "kind": "round-open-seam", "width": width, "depth": depth,
            "wallThickness": p.get("wallThickness", 0), "cornerRadius": p.get("cornerRadius", 0),
            "specification": f"纵向开缝圆管 {width:g} × {depth:g} mm"}
