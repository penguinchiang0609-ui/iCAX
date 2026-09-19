"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        measured=q.box(loops[0],g,t) or q.capsule(loops[0],g,t)
        if measured:
            radius=measured["radius"]
            return q.result({"width":measured["width"],
                             "depth":measured["depth"],
                             "cornerRadius":radius,
                             "useHotRolled":radius>t},pose)
    return False
