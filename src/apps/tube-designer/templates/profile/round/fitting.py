"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q=context["geometry"];t=context["tolerance"]
    measured=q.eccentric_circles(section,t)
    if measured is False:return False
    (outer_radius,inner_radius,offset_x,offset_y),pose=measured
    parameters={"width":2*outer_radius,
                "wallThickness":outer_radius-inner_radius,
                "innerOffsetX":offset_x,
                "innerOffsetY":offset_y}
    return q.result(parameters,pose)
