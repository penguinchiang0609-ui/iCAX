"""Direct geometric inverse for the single U-shaped channel model."""
import math


IMPLEMENTED=True


HOT_SLOPE = 0.1


def _direction_matches(a, b, kind, sx, sy, tolerance):
    dx=b[0]-a[0]
    dy=b[1]-a[1]
    length=math.hypot(dx,dy)
    if length<=tolerance:return False
    if kind == "h":
        return abs(dy)<=tolerance and dx*sx>0
    if kind == "v":
        return abs(dx)<=tolerance and dy*sy>0
    if kind == "s":
        if dx*sx<=0 or dy*sy<=0 or abs(dx)<=tolerance:return False
        return abs(abs(dy/dx)-HOT_SLOPE)<=max(1e-5,10*tolerance)
    return False


def _fit_hot_rolled(corners, tolerance):
    if not corners or len(corners)!=8:return False
    # Outer lower toe -> outer lower web root -> outer upper web root ->
    # outer upper toe -> inner upper toe -> inner upper web root ->
    # inner lower web root -> inner lower toe.
    pattern=(("h",-1,0),("v",0,1),("h",1,0),("v",0,-1),
             ("s",-1,-1),("v",0,-1),("s",1,-1),("v",0,-1))
    for orientation in (corners, list(reversed(corners))):
      for shift in range(8):
        c=orientation[shift:]+orientation[:shift]
        if any(not _direction_matches(c[i][0],c[(i+1)%8][0],*pattern[i],tolerance)
               for i in range(8)):
            continue
        radii=[abs(float(entry[1])) for entry in c]
        if any(not math.isfinite(radius) or radius< -tolerance for radius in radii):
            continue
        if any(radii[index]>tolerance for index in (0,3)):
            continue
        x=[entry[0][0] for entry in c]
        y=[entry[0][1] for entry in c]
        width=x[3]-x[1]
        depth=y[2]-y[0]
        wall_span=x[5]-x[1]
        if min(width,depth,wall_span)<=tolerance:
            continue
        bottom_mid=(y[6]+y[7])/2
        top_mid=(y[4]+y[5])/2
        bottom_flange=bottom_mid-y[0]
        top_flange=y[2]-top_mid
        if min(bottom_flange,top_flange)<=tolerance or abs(bottom_flange-top_flange)>tolerance:
            continue
        offset_y_bottom=bottom_mid-y[0]-(bottom_flange+top_flange)/2
        offset_y_top=top_mid-y[2]+(bottom_flange+top_flange)/2
        if abs(offset_y_bottom-offset_y_top)>tolerance:
            continue
        outer_radii=[radii[1],radii[2]]
        inner_radii=[radii[6],radii[5]]
        free_radii=[radii[7],radii[4]]
        use_outer=abs(outer_radii[0]-outer_radii[1])>tolerance
        use_inner=any(abs(outer-inner-wall_span)>tolerance
                      for outer,inner in zip(outer_radii,inner_radii))
        independent_free=abs(free_radii[0]-free_radii[1])>tolerance
        return {
            "width":width,
            "depth":depth,
            "wallThickness":wall_span,
            "outerRadius":outer_radii[0],
            "useHotRolled":True,
            "flangeThickness":(bottom_flange+top_flange)/2,
            "useOuterRadii":use_outer,
            "outerRadius1":outer_radii[0],
            "outerRadius2":outer_radii[1],
            "useInnerRadius":use_inner,
            "innerRadius1":inner_radii[0],
            "innerRadius2":inner_radii[1],
            "freeEndRadius":free_radii[0] if not independent_free else 0,
            "useIndependentFreeEndRadii":independent_free,
            "freeEndRadius1":free_radii[0],
            "freeEndRadius2":free_radii[1],
            "innerOffsetX":0,
            "innerOffsetY":(offset_y_bottom+offset_y_top)/2,
        }, [0,0]
    return False


def _fit_channel(loop,tolerance):
    if loop.get("kind") != "path": return False
    edges=loop.get("edges",[])
    lines=[e for e in edges if e.get("kind")=="line"]
    arcs=[e for e in edges if e.get("kind")=="arc"]
    if len(lines) != 8 or len(arcs)>6: return False

    horizontal=[e for e in lines if abs(e["start"][1]-e["end"][1])<=tolerance]
    vertical=[e for e in lines if abs(e["start"][0]-e["end"][0])<=tolerance]
    if len(horizontal)!=4 or len(vertical)!=4: return False

    length=lambda e:math.dist(e["start"],e["end"])
    web=sorted(vertical,key=length,reverse=True)[:2]
    ends=sorted(vertical,key=length)[:2]
    if any(length(e)<=tolerance for e in web+ends): return False
    if length(web[1])<=length(ends[0])+tolerance: return False
    web_x=[e["start"][0] for e in web]
    web_center=sum(web_x)/2
    end_x=[e["start"][0] for e in ends]
    free_x=sum(end_x)/2
    wall=abs(web_x[0]-web_x[1])
    if wall<=tolerance or abs(end_x[0]-end_x[1])>tolerance: return False
    if free_x<=web_center+tolerance: return False

    levels=sorted(e["start"][1] for e in horizontal)
    if any(abs(a-b)<=tolerance for a,b in zip(levels,levels[1:])): return False
    bottom=sum(levels[:2])/2
    top=sum(levels[2:])/2
    depth=top-bottom
    width=free_x-web_center
    if min(width,depth)<=tolerance: return False
    if abs((levels[1]-levels[0])-wall)>tolerance: return False
    if abs((levels[3]-levels[2])-wall)>tolerance: return False

    def adjacent(arc,line):
        return any(math.dist(arc[end],line[start])<=tolerance
                   for end in ("start","end") for start in ("start","end"))
    root_arcs=[arc for arc in arcs if any(adjacent(arc,line) for line in web)]
    free_arcs=[arc for arc in arcs if arc not in root_arcs]
    if len(root_arcs)!=4 or not 0<=len(free_arcs)<=2: return False
    outer_radii=[0,0]
    inner_radii=[0,0]
    for edge in root_arcs:
        radius=float(edge.get("radius",-1))
        if radius<0 or not math.isfinite(radius): return False
        matches=[i for i,level in enumerate(levels)
                 if abs(edge["start"][1]-level)<=tolerance
                 or abs(edge["end"][1]-level)<=tolerance]
        if len(matches)!=1: return False
        index=matches[0]
        if index==0: slot=outer_radii; side=0
        elif index==3: slot=outer_radii; side=1
        elif index==1: slot=inner_radii; side=0
        else: slot=inner_radii; side=1
        if slot[side] and abs(slot[side]-radius)>tolerance: return False
        slot[side]=radius
    if any(outer<inner-tolerance for outer,inner in zip(outer_radii,inner_radii)): return False
    free_radii=[0,0]
    for edge in free_arcs:
        radius=float(edge.get("radius",-1))
        if radius<0 or not math.isfinite(radius): return False
        matches=[i for i,level in enumerate(levels)
                 if abs(edge["start"][1]-level)<=tolerance
                 or abs(edge["end"][1]-level)<=tolerance]
        if len(matches)!=1 or matches[0] not in (1,2): return False
        free_radii[0 if matches[0]==1 else 1]=radius
    independent_free=abs(free_radii[0]-free_radii[1])>tolerance
    use_inner=any(abs(outer-inner-wall)>tolerance
                  for outer,inner in zip(outer_radii,inner_radii))
    use_outer=abs(outer_radii[0]-outer_radii[1])>tolerance
    parameters=dict(width=width,depth=depth,wallThickness=wall,
                    outerRadius=outer_radii[0],useOuterRadii=use_outer,
                    outerRadius1=outer_radii[0],outerRadius2=outer_radii[1],
                    useInnerRadius=use_inner,
                    innerRadius1=inner_radii[0],innerRadius2=inner_radii[1],
                    freeEndRadius=free_radii[0] if not independent_free else 0,
                    useIndependentFreeEndRadii=independent_free,
                    freeEndRadius1=free_radii[0],freeEndRadius2=free_radii[1],
                    innerOffsetX=0,innerOffsetY=0,
                    useHotRolled=False,flangeThickness=8.5)
    origin=[(web_center+free_x)/2,(bottom+top)/2]
    return parameters,origin


def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        corners=q.polygon_corners(loops[0],t)
        result=_fit_hot_rolled(corners,t) or _fit_channel(loops[0],t)
        if result:
            parameters,origin=result
            return q.result(parameters,q.shifted_pose(pose,origin))
    return False
