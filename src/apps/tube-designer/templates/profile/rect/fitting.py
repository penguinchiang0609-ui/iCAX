"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    for loops,pose in g.frames(section):
        measured=q.paired_sections(loops,g,t,q.box)
        if measured:
            outer,wall=measured
            return q.result({"width":outer["width"],"depth":outer["depth"],"wallThickness":wall,
                "cornerRadius":outer["radius"],"innerWidth":0,"innerDepth":0,"innerOffsetX":0,"innerOffsetY":0,
                "outerRadii":"","innerRadii":"","outerCorners":"","innerCorners":""},pose)
    return False
