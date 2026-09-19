"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    import math
    q,t=context["geometry"],context["tolerance"]
    if len(section)!=2 or any(c["kind"] not in ("ellipse","circle") for c in section):return False
    a,b=section
    major=a.get("a",a.get("radius"));minor=a.get("b",a.get("radius"))
    imajor=b.get("a",b.get("radius"));iminor=b.get("b",b.get("radius"))
    if min(major-imajor,minor-iminor)<=t:return False
    angle=a.get("angle",0)
    if b["kind"]=="ellipse" and abs(math.sin(b["angle"]-angle))*imajor>t:return False
    dx=b["center"][0]-a["center"][0]
    dy=b["center"][1]-a["center"][1]
    values={"width":2*major,"depth":2*minor,
            "innerOffsetX":dx*math.cos(angle)+dy*math.sin(angle),
            "innerOffsetY":-dx*math.sin(angle)+dy*math.cos(angle)}
    if abs((major-imajor)-(minor-iminor))<=t:
        values.update(wallThickness=major-imajor,model0InnerWidth=0,model0InnerDepth=0)
    else:
        values.update(model0InnerWidth=2*imajor,model0InnerDepth=2*iminor)
    return q.result(values,{"rotation":angle,"translation":a["center"]})
