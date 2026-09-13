"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    import math
    q,t=context["geometry"],context["tolerance"]
    if len(section)!=1 or section[0]["kind"]!="path":return False
    edges=section[0]["edges"]
    arcs=sorted([e for e in edges if e["kind"]=="arc"],key=lambda e:-e["radius"])
    lines=[e for e in edges if e["kind"]=="line"]
    if len(arcs)!=2 or len(lines)!=2:return False
    outer,inner=arcs;r,ri=outer["radius"],inner["radius"];center=outer["center"]
    if ri<=t or r-ri<=t or math.dist(center,inner["center"])>t:return False
    if outer["sweep"]<=0 or inner["sweep"]>=0 or abs(outer["sweep"]+inner["sweep"])*r>t:return False
    gap=math.tau-outer["sweep"]
    if not 0<gap<math.tau:return False
    for e in lines:
        vectors=[[p[i]-center[i] for i in (0,1)] for p in (e["start"],e["end"])]
        a,b=vectors
        if abs(a[0]*b[1]-a[1]*b[0])>t*r or sum(x*y for x,y in zip(a,b))<=0:return False
        if abs(math.dist(e["start"],e["end"])-(r-ri))>t:return False
    angle=math.atan2(outer["start"][1]-center[1],outer["start"][0]-center[0])-gap/2
    return q.result({"width":2*r,"wallThickness":r-ri,"openingAngle":math.degrees(gap)},
                    {"rotation":angle,"translation":center},"unique")
