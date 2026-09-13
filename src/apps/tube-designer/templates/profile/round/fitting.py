"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    q=context["geometry"];t=context["tolerance"]
    measured=q.concentric_circles(section,t,2)
    if measured is False:return False
    radii,pose=measured
    parameters={"width":2*radii[0]}
    parameters["wallThickness"]=radii[0]-radii[1]
    return q.result(parameters,pose)
