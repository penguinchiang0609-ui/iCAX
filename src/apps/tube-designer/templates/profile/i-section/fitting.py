"""Direct geometric inverse for the generic and hot-rolled I-section."""
import math


IMPLEMENTED = True
SLOPE = 1.0 / 6.0


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
        return abs(abs(dy/dx)-SLOPE)<=max(1e-5,10*tolerance)
    return False


def _fit_hot_rolled(corners, tolerance):
    if not corners or len(corners)!=12:return False
    # Starting at the lower-left outside corner, the hot-rolled contour is:
    # outer flange, toe arc/line, 1:6 slope, web, upper slope, toe, outer
    # flange, then the mirrored left half. Try every path start.
    pattern=(("h",1,0),("v",0,1),("s",-1,1),("v",0,1),
             ("s",1,1),("v",0,1),("h",-1,0),("v",0,-1),
             ("s",1,-1),("v",0,-1),("s",-1,-1),("v",0,-1))
    for shift in range(12):
        c=corners[shift:]+corners[:shift]
        if any(not _direction_matches(c[i][0],c[(i+1)%12][0],*pattern[i],tolerance)
               for i in range(12)):
            continue
        radii=[float(entry[1]) for entry in c]
        if any(not math.isfinite(radius) or radius< -tolerance for radius in radii):
            continue
        if any(abs(radii[i])>tolerance for i in (0,1,6,7)):
            continue
        top_width=c[6][0][0]-c[7][0][0]
        bottom_width=c[1][0][0]-c[0][0][0]
        height=c[6][0][1]-c[0][0][1]
        web=c[3][0][0]-c[10][0][0]
        web_offset=(c[3][0][0]+c[10][0][0])/2
        if min(top_width,bottom_width,height,web)<=tolerance:
            continue
        top_mid_y=(c[4][0][1]+c[5][0][1])/2
        bottom_mid_y=(c[2][0][1]+c[3][0][1])/2
        top_flange=c[6][0][1]-top_mid_y
        bottom_flange=bottom_mid_y-c[0][0][1]
        if min(top_flange,bottom_flange)<=tolerance or abs(top_flange-bottom_flange)>tolerance:
            continue
        root_radii=[radii[3],radii[4],radii[9],radii[10]]
        toe_radii=[radii[2],radii[5],radii[8],radii[11]]
        if any(abs(radius-toe_radii[0])>tolerance for radius in toe_radii[1:]):
            continue
        use_widths=abs(top_width-bottom_width)>tolerance
        use_radii=any(abs(radius-root_radii[0])>tolerance for radius in root_radii[1:])
        width=top_width if not use_widths else max(top_width,bottom_width)
        return {
            "width":width,
            "depth":height,
            "rootRadius":root_radii[0],
            "wallThickness":web,
            "flangeThickness":(top_flange+bottom_flange)/2,
            "useHotRolled":True,
            "useIndependentFlangeWidths":use_widths,
            "topFlangeWidth":top_width,
            "bottomFlangeWidth":bottom_width,
            "webOffset":web_offset,
            "useIndependentRadii":use_radii,
            "rootRadius1":root_radii[0],
            "rootRadius2":root_radii[1],
            "rootRadius3":root_radii[2],
            "rootRadius4":root_radii[3],
            "legEndRadius":toe_radii[0],
        }
    return False


def _fit_generic(corners, tolerance):
    if not corners or len(corners)!=12:return False
    for shift in range(12):
        c=corners[shift:]+corners[:shift]
        x=[corner[0][0] for corner in c]
        y=[corner[0][1] for corner in c]
        radii=[abs(corner[1]) for corner in c]
        if any(abs(y[a]-y[b])>tolerance for a,b in ((0,1),(2,3),(4,5),(6,7),(8,9),(10,11))):
            continue
        if any(abs(x[a]-x[b])>tolerance for a,b in ((1,2),(3,4),(5,6),(7,8),(9,10),(11,0))):
            continue
        top_width=x[6]-x[7]
        bottom_width=x[1]-x[0]
        depth=y[6]-y[0]
        wall=x[3]-x[9]
        offset=(x[3]+x[9])/2
        lower_flange=y[2]-y[0]
        upper_flange=y[6]-y[5]
        if min(top_width,bottom_width,depth,wall,lower_flange,upper_flange,
               y[4]-y[3])<=tolerance:
            continue
        if min(top_width-2*abs(offset)-wall,
               bottom_width-2*abs(offset)-wall)<=tolerance:
            continue
        if any(radii[index]>tolerance for index in (0,1,2,5,6,7)):
            continue
        if abs(lower_flange-upper_flange)>tolerance:
            continue
        roots=[radii[3],radii[4],radii[9],radii[10]]
        if any(abs(radii[index])>tolerance for index in (8,11)):
            continue
        use_widths=abs(top_width-bottom_width)>tolerance
        independent=any(abs(radius-roots[0])>tolerance for radius in roots[1:])
        return {
            "width":top_width if not use_widths else max(top_width,bottom_width),
            "depth":depth,"rootRadius":roots[0],
            "wallThickness":wall,"flangeThickness":lower_flange,
            "useHotRolled":False,
            "useIndependentFlangeWidths":use_widths,
            "topFlangeWidth":top_width,"bottomFlangeWidth":bottom_width,
            "webOffset":offset,"useIndependentRadii":independent,
            "rootRadius1":roots[0],"rootRadius2":roots[1],
            "rootRadius3":roots[2],"rootRadius4":roots[3],
            "legEndRadius":0,
        }
    return False


def fitting(section, context):
    q,geometry,tolerance=context["geometry"],context["curves"],context["tolerance"]
    if len(section)!=1:
        return False
    for loops,pose in geometry.frames(section):
        corners=q.polygon_corners(loops[0],tolerance)
        result=_fit_hot_rolled(corners,tolerance) or _fit_generic(corners,tolerance)
        if result:
            return q.result(result,pose)
    return False
