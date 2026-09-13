"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    circles=q.concentric_circles(section,t,2)
    if circles:
        radii,pose=circles
        return q.result({"width":2*radii[0],"depth":2*radii[0],"wallThickness":radii[0]-radii[1]},pose)
    for loops,pose in g.frames(section):
        measured=q.paired_sections(loops,g,t,q.capsule)
        if measured:
            outer,wall=measured
            return q.result({"width":outer["width"],"depth":outer["depth"],"wallThickness":wall},pose)
    return False
