"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    import math
    q,t=context['geometry'],context['tolerance']
    if len(section)!=2:return False
    outer,inner=section
    if outer['kind']=='circle':
        poly=q.regular_polygon(inner,6,t)
        if not poly or math.dist(poly['center'],outer['center'])>t:return False
        if outer['radius']-poly['radius']<=t:return False
        # The circular exterior cannot distinguish inner phase from placement.
        return q.result({'sectionModel':'round-hex-bore','width':2*outer['radius'],
            'model0InnerSize':poly['size'],'model0InnerPhase':0},
            {'rotation':poly['phase'],'translation':outer['center']})
    if inner['kind']=='circle':
        poly=q.regular_polygon(outer,6,t)
        if not poly or math.dist(poly['center'],inner['center'])>t:return False
        if poly['size']/2-inner['radius']<=t:return False
        return q.result({'sectionModel':'hex-round-bore','width':poly['size'],
            'model1InnerDiameter':2*inner['radius']},
            {'rotation':poly['phase'],'translation':poly['center']})
    return False
