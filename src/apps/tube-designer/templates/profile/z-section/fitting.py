"""Direct geometric inverse for cold-formed Z sections."""
import math

IMPLEMENTED=True


def _angle(a,b):
    la,lb=math.hypot(*a),math.hypot(*b)
    if min(la,lb)<=1e-12:return None
    cosine=max(-1.0,min(1.0,sum(a[i]*b[i] for i in (0,1))/(la*lb)))
    return math.degrees(math.acos(cosine))


def fitting(section,context):
    q,g,t=context["geometry"],context["curves"],context["tolerance"]
    if len(section)!=1:return False
    for loops,pose in g.frames(section):
        for raw,wall,radius in q.strip_axes(loops[0],t):
            for points in (raw,list(reversed(raw))):
                if len(points) not in (4,6):continue
                if len(points)==4:
                    lower,web_bottom,web_top,upper=points
                    top_lip=bottom_lip=0.0
                    top_angle=bottom_angle=90.0
                else:
                    bottom_free,lower,web_bottom,web_top,upper,top_free=points
                    bottom_lip=math.dist(bottom_free,lower)
                    top_lip=math.dist(upper,top_free)
                    bottom_angle=_angle([web_bottom[i]-lower[i] for i in (0,1)],
                                        [bottom_free[i]-lower[i] for i in (0,1)])
                    top_angle=_angle([web_top[i]-upper[i] for i in (0,1)],
                                     [top_free[i]-upper[i] for i in (0,1)])
                    if None in (bottom_angle,top_angle):continue
                if (abs(lower[1]-web_bottom[1])>t or abs(web_bottom[0]-web_top[0])>t
                        or abs(web_top[1]-upper[1])>t):continue
                bottom=web_bottom[0]-lower[0]
                top=upper[0]-web_top[0]
                height=web_top[1]-web_bottom[1]+wall
                if min(top,bottom,height,wall)<=t:continue
                has_lips=len(points)==6
                independent=(has_lips and
                    (abs(top_lip-bottom_lip)>t or abs(top_angle-bottom_angle)>1e-5))
                params={
                    "depth":height,
                    "topFlangeWidth":top,
                    "bottomFlangeWidth":bottom,
                    "wallThickness":wall,
                    "bendRadius":radius,
                    "useLips":has_lips,
                    "lipLength":(top_lip+bottom_lip)/2 if has_lips else 14.0,
                    "lipAngle":(top_angle+bottom_angle)/2 if has_lips else 90.0,
                    "useIndependentLips":independent,
                    "topLipLength":top_lip if has_lips else 14.0,
                    "bottomLipLength":bottom_lip if has_lips else 14.0,
                    "topLipAngle":top_angle if has_lips else 90.0,
                    "bottomLipAngle":bottom_angle if has_lips else 90.0,
                }
                origin=[web_bottom[0],(web_bottom[1]+web_top[1])/2]
                return q.result(params,q.shifted_pose(pose,origin))
    return False
