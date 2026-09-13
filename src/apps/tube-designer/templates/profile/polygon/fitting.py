"""Analytic regular-polygon and parallel-offset polygon inverse."""
import math
IMPLEMENTED=True

def fitting(section,context):
    q,g,t=context['geometry'],context['curves'],context['tolerance']
    if len(section)!=2:return False
    corners=[q.polygon_corners(c,t) for c in section]
    if not all(corners) or len(corners[0])!=len(corners[1]):return False
    n=len(corners[0])
    if n in (3,5,6,8):
        measured=[q.regular_vertices([p for p,r in c],t) for c in corners]
        if all(measured):
            a,b=measured
            if math.dist(a['center'],b['center'])>t or a['size']-b['size']<=t:return False
            index,name={3:(1,'triangle-equilateral'),5:(6,'pentagonal-tube'),6:(0,'hexagonal-tube'),8:(7,'octagonal-tube')}[n]
            angle=a['phase'];period=math.tau/n
            phase=(b['phase']-angle)%period
            if phase>period-1e-8:phase=0
            radii=[]
            for cs,m,base in ((corners[0],a,angle),(corners[1],b,angle+phase)):
                def position(c):
                    value=(math.atan2(c[0][1]-m['center'][1],c[0][0]-m['center'][0])-base)%math.tau
                    return 0 if value>math.tau-1e-8 else value
                ordered=sorted(cs,key=position)
                radii.append(','.join(format(c[1],'.12g') for c in ordered))
            params=dict(sectionModel=name,width=a['size'])
            params.update({f'model{index}InnerSize':b['size'],f'model{index}InnerPhase':math.degrees(phase),
                f'model{index}OuterRadii':radii[0],f'model{index}InnerRadii':radii[1]})
            return q.result(params,dict(rotation=angle,translation=a['center']))
    if n not in (3,4) or any(r>t for cs in corners for p,r in cs):return False
    for loops,pose in g.frames(section):
        outer=[e['start'] for e in loops[0]['edges']]
        inner=[e['start'] for e in loops[1]['edges']]
        if len(outer)!=n or len(inner)!=n:continue
        for k in range(n):
            p=outer[k:]+outer[:k];x=[a[0] for a in p];y=[a[1] for a in p]
            if abs(y[0]-y[1])>t:continue
            w=x[1]-x[0];h=y[2]-y[0]
            if min(w,h)<=t:continue
            params=dict(width=w,depth=h)
            cx=(x[0]+x[1])/2;cy=(y[0]+y[2])/2
            if n==3:
                params.update(sectionModel='triangle-scalene',model2ApexOffset=x[2]-cx)
            else:
                if abs(y[2]-y[3])>t:continue
                top=x[2]-x[3]
                if top<=t:continue
                if abs(w-top)<=t:
                    shear=x[3]-x[0];cx+=shear/2
                    params.update(sectionModel='parallelogram-tube',model5Shear=shear)
                elif w>top+t and abs(x[3]-x[0])<=t:
                    params.update(sectionModel='trapezoid-right',model3TopWidth=top)
                elif w>top+t and abs(x[2]+x[3]-2*cx)<=t:
                    params.update(sectionModel='trapezoid-isosceles',model4TopWidth=top)
                else:continue
            # One-to-one parallel inner support edges, all at the same inward distance.
            distances=[];used=set()
            for i,a in enumerate(p):
                b=p[(i+1)%n];length=math.dist(a,b)
                u=[(b[d]-a[d])/length for d in (0,1)]
                choices=[]
                for j,c in enumerate(inner):
                    d=inner[(j+1)%n];v=[d[z]-c[z] for z in (0,1)]
                    if j in used or abs(u[0]*v[1]-u[1]*v[0])>t or sum(u[z]*v[z] for z in (0,1))<=t:continue
                    gap=u[0]*(c[1]-a[1])-u[1]*(c[0]-a[0])
                    if gap>t:choices.append((j,gap))
                if len(choices)!=1:break
                j,gap=choices[0];used.add(j);distances.append(gap)
            if len(distances)!=n or max(distances)-min(distances)>t:continue
            params['wallThickness']=sum(distances)/n
            return q.result(params,q.shifted_pose(pose,[cx,cy]))
    return False
