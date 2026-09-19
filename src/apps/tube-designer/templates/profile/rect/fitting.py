"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    for loops,pose in g.frames(section):
        measured=q.paired_rectangles(loops,g,t)
        if measured:
            outer,inner,wall=measured
            outer_radii=outer["radii"]
            inner_radii=inner["radii"]
            uniform_outer=max(outer_radii)-min(outer_radii)<=t
            uniform_inner=max(inner_radii)-min(inner_radii)<=t
            return q.result({
                "width":outer["width"], "depth":outer["depth"], "wallThickness":wall,
                "cornerRadius":outer_radii[0], "innerRadius":inner_radii[0], "useOuterRadii":not uniform_outer,
                "outerRadius1":outer_radii[0], "outerRadius2":outer_radii[1], "outerRadius3":outer_radii[2], "outerRadius4":outer_radii[3],
                "useInnerRadii":not uniform_inner,
                "innerRadius1":inner_radii[0], "innerRadius2":inner_radii[1], "innerRadius3":inner_radii[2], "innerRadius4":inner_radii[3],
                "innerOffsetX":inner["center"][0]-outer["center"][0],
                "innerOffsetY":inner["center"][1]-outer["center"][1],
            },pose)
    return False
