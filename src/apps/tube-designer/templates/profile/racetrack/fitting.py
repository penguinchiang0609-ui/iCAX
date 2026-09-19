"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    circles=q.eccentric_circles(section,t)
    if circles:
        (outer_radius,inner_radius,offset_x,offset_y),pose=circles
        return q.result({"width":2*outer_radius,"depth":2*outer_radius,
                         "wallThickness":outer_radius-inner_radius,
                         "innerOffsetX":offset_x,"innerOffsetY":offset_y},pose)
    for loops,pose in g.frames(section):
        measured=q.paired_sections_offset(loops,g,t,q.capsule)
        if measured:
            outer,wall,offset=measured
            return q.result({"width":outer["width"],"depth":outer["depth"],
                             "wallThickness":wall,"innerOffsetX":offset[0],"innerOffsetY":offset[1]},pose)
    return False
