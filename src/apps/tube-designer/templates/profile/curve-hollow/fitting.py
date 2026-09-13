"""Direct geometric inverse; no calls to the forward generator."""
IMPLEMENTED = True


def fitting(section, context):
    import math
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=2:return False
    for loops,pose in g.frames(section):
        measured=[]
        for loop in loops:
            if loop['kind']!='path' or len(loop['edges'])!=2:break
            lines=[e for e in loop['edges'] if e['kind']=='line']
            arcs=[e for e in loop['edges'] if e['kind']=='arc']
            if len(lines)!=1 or len(arcs)!=1:break
            line,arc=lines[0],arcs[0]
            a,b=line['start'],line['end'];cx,cy=arc['center'];r=arc['radius']
            if abs(a[0]-b[0])>t or abs(a[1]+b[1]-2*cy)>t:break
            if r<=t or arc['sweep']<=0:break
            angle=math.atan2(arc['start'][1]-cy,arc['start'][0]-cx)+arc['sweep']/2
            if abs(math.sin(angle))*r>t or math.cos(angle)<0:break
            measured.append((arc,a[0]))
        if len(measured)!=2:continue
        (outer,x),(inner,ix)=measured
        r=outer['radius'];wall=r-inner['radius']
        if wall<=t or r-2*wall<=t:continue
        if math.dist(outer['center'],inner['center'])>t:continue
        if abs(x-outer['center'][0])>t or abs(ix-x-wall)>t:continue
        if abs(outer['sweep']-math.pi)*r>t:continue
        return q.result({'width':r,'wallThickness':wall},pose,'unique')
    return False
