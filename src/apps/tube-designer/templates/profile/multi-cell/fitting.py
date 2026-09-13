"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    if len(section)!=3:return False
    for loops,pose in g.frames(section):
        boxes=[q.box(c,g,t) for c in loops]
        if not all(boxes) or any(b["radius"]>t for b in boxes):continue
        outer=boxes[0];left,right=sorted(boxes[1:],key=lambda b:b["center"][0])
        w,h=outer["width"],outer["depth"];wall=(h-left["depth"])/2
        if wall<=t or abs(right["depth"]-left["depth"])>t:continue
        if abs(left["center"][1])>t or abs(right["center"][1])>t:continue
        lx=left["center"][0]-left["width"]/2;rx=right["center"][0]+right["width"]/2
        if abs(lx-(-w/2+wall))>t or abs(rx-(w/2-wall))>t:continue
        a=left["center"][0]+left["width"]/2;b=right["center"][0]-right["width"]/2
        if b-a<=t:continue
        return q.result({"width":w,"depth":h,"wallThickness":wall,"ribThickness":b-a,"ribOffset":(a+b)/2},pose)
    return False
