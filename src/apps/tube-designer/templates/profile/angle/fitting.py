"""Direct geometric inverse for the single L-shaped angle model."""
import math

IMPLEMENTED=True


def _fit_angle(loop,tolerance):
    if loop.get("kind")!="path":return False
    edges=loop.get("edges",[])
    lines=[e for e in edges if e.get("kind")=="line"]
    arcs=[e for e in edges if e.get("kind")=="arc"]
    if len(lines)!=6 or not 2<=len(arcs)<=4:return False

    horizontal=[e for e in lines if abs(e["start"][1]-e["end"][1])<=tolerance]
    vertical=[e for e in lines if abs(e["start"][0]-e["end"][0])<=tolerance]
    if len(horizontal)!=3 or len(vertical)!=3:return False
    length=lambda e:math.dist(e["start"],e["end"])
    long_horizontal=sorted(horizontal,key=length,reverse=True)[:2]
    long_vertical=sorted(vertical,key=length,reverse=True)[:2]
    short_horizontal=[e for e in horizontal if e not in long_horizontal]
    short_vertical=[e for e in vertical if e not in long_vertical]
    if len(short_horizontal)!=1 or len(short_vertical)!=1:return False
    wall_x=abs(long_vertical[0]["start"][0]-long_vertical[1]["start"][0])
    wall_y=abs(long_horizontal[0]["start"][1]-long_horizontal[1]["start"][1])
    if wall_x<=tolerance or abs(wall_x-wall_y)>tolerance:return False
    center_x=sum(e["start"][0] for e in long_vertical)/2
    center_y=sum(e["start"][1] for e in long_horizontal)/2
    signed_width=short_vertical[0]["start"][0]-center_x
    signed_depth=short_horizontal[0]["start"][1]-center_y
    # Rotated frame candidates preserve the two public leg dimensions.  The
    # sign of the horizontal leg distinguishes the canonical and mirrored
    # forms without invoking the forward generator.
    if abs(signed_width)<=tolerance or signed_depth<=tolerance:return False
    mirror_x=signed_width<0
    width=abs(signed_width)
    depth=signed_depth
    if min(width,depth)<=wall_x+tolerance:return False
    def adjacent(arc,line):
        return any(math.dist(arc[end],line[start])<=tolerance
                   for end in ("start","end") for start in ("start","end"))
    long_lines=long_horizontal+long_vertical
    root_arcs=[arc for arc in arcs if sum(adjacent(arc,line) for line in long_lines)==2]
    free_arcs=[arc for arc in arcs if arc not in root_arcs]
    if len(root_arcs)!=2 or len(free_arcs)!=len(arcs)-2:return False
    radii=sorted(float(e.get("radius",-1)) for e in root_arcs)
    if radii[0]<0 or radii[1]<radii[0]-tolerance:return False
    # The larger radius is the outside bend; unlike strip_axes this does not
    # assume that its value is the inside radius plus the wall thickness.
    outer_radius,inner_radius=radii[1],radii[0]
    if outer_radius < inner_radius-tolerance:return False
    free_horizontal=next((arc for arc in free_arcs
                          if adjacent(arc,short_vertical[0])),None)
    free_vertical=next((arc for arc in free_arcs
                        if adjacent(arc,short_horizontal[0])),None)
    if any(arc not in (free_horizontal,free_vertical) for arc in free_arcs):return False
    free1=float(free_horizontal.get("radius",-1)) if free_horizontal else 0.0
    free2=float(free_vertical.get("radius",-1)) if free_vertical else 0.0
    if min(free1,free2)<0:return False
    independent_free=abs(free1-free2)>tolerance
    parameters=dict(width=width,depth=depth,wallThickness=wall_x,
                    useHotRolled=bool(free1>tolerance or free2>tolerance),
                    outerRadius=outer_radius,
                    useInnerRadius=abs(outer_radius-inner_radius-wall_x)>tolerance,
                    innerRadius=inner_radius,
                    freeEndRadius=free1 if not independent_free else 0,
                    useIndependentFreeEndRadii=independent_free,
                    freeEndRadius1=free1,freeEndRadius2=free2,
                    innerOffsetX=0,innerOffsetY=0,mirrorX=mirror_x)
    # ``frames`` centers the measured loop on its envelope, while the
    # parameterized angle profile is anchored at the theoretical centerline
    # corner ``(-width/2, -depth/2)`` (plus the two offsets).  Convert that
    # local centerline point into the profile build origin before returning
    # the pose; shifting by the centerline point itself double-counts it.
    build_origin=[center_x+(-width/2 if mirror_x else width/2)-parameters["innerOffsetX"],
                  center_y+depth/2-parameters["innerOffsetY"]]
    return parameters,build_origin


def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        result=_fit_angle(loops[0],t)
        if result:
            parameters,origin=result
            return q.result(parameters,q.shifted_pose(pose,origin))
    return False
