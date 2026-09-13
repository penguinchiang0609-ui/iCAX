"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    import math
    q,t=context["geometry"],context["tolerance"]
    if len(section)!=1 or section[0]["kind"]!="path":return False
    edges=section[0]["edges"];n=len(edges)
    if n not in (6,8) or any(e["kind"]!="line" for e in edges):return False
    points=[e["start"] for e in edges]
    center=[sum(p[i] for p in points)/n for i in (0,1)]
    radius=math.dist(points[0],center)
    if radius<=t:return False
    angles=[math.atan2(p[1]-center[1],p[0]-center[0]) for p in points]
    for i,p in enumerate(points):
        if abs(math.dist(p,center)-radius)>t:return False
        if abs((angles[(i+1)%n]-angles[i])%math.tau-math.tau/n)*radius>t:return False
    return q.result({"sectionModel":"hexagonal-bar" if n==6 else "octagonal-bar",
        "width":2*radius*math.cos(math.pi/n)},{"rotation":angles[0]%(math.tau/n),"translation":center})
