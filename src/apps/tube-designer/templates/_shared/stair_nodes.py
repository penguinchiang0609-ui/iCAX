"""Section-driven stair joints. No profile-ID dispatch or bounding-box copes."""
import math


def add(a, b): return [x+y for x,y in zip(a,b)]
def sub(a, b): return [x-y for x,y in zip(a,b)]
def mul(a, s): return [x*s for x in a]
def dot(a, b): return sum(x*y for x,y in zip(a,b))
def unit(a):
    n=math.sqrt(dot(a,a))
    if n<1e-9: raise ValueError("节点方向退化")
    return mul(a,1/n)
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]


class RailNetwork:
    """Straight stock pieces with actual bisector cuts at shared endpoints.

    Slope-to-level transitions and plan turns are separate planar joints. This
    preserves section roll for non-circular rails rather than twisting a single
    diagonal connector through two unrelated planes.
    """
    def __init__(self):
        self.edges=[]
        self.connections=[]
        self.vertical_axes={}

    def add(self,key,a,b,pf,placement,connect_start=False,connect_end=False,platform=False):
        def world(v):
            return [placement['origin'][i]+sum(v[j]*placement[k][i] for j,k in enumerate(('xAxis','yAxis','zAxis'))) for i in range(3)]
        a,b=world(a),world(b)
        self.edges.append((key,a,b,pf,platform))
        direction=unit(sub(b,a));horizontal=unit([direction[0],direction[1],0])
        if connect_start:self.connections.append((a,mul(horizontal,-1),pf,True))
        if connect_end:self.connections.append((b,horizontal,pf,False))

    def finish(self,tube,prism,boolean,item,relationship):
        def ident(v):return tuple(round(x,6) for x in v)
        counts={}
        for _,a,b,_,platform in self.edges:
            if platform:
                for v in (a,b):counts[ident(v)]=counts.get(ident(v),0)+1
        available=[list(v) for v,n in counts.items() if n==1]
        consumed=set()
        for index,(a,d,pf,upper) in enumerate(self.connections):
            if index in consumed:continue
            candidates=[v for v in available if v[2]<a[2]-1e-5] if upper else [v for v in available if abs(v[2]-a[2])<1e-5]
            # A narrow U well has no independent platform rail between lanes.
            # Join the two flight ends directly when that is the nearest route.
            pairs=[(j,data) for j,data in enumerate(self.connections) if j>index and j not in consumed and data[3]!=upper]
            if not upper and pairs:
                j,(end,back,other,_) = min(pairs,key=lambda v:math.dist(a,v[1][0]))
                platform_distance=min((math.dist(a,v) for v in candidates),default=math.inf)
                if math.dist(a,end)<platform_distance:
                    if math.dist(a[:2],end[:2])<max(pf.width,pf.depth):
                        raise ValueError('两跑内侧扶手净距不足，请调整井道间距或扶手规格')
                    lead=max(120.,3*max(pf.width,pf.depth))
                    low=[end[0],end[1],a[2]]
                    points=[a,add(a,mul(d,lead)),add(low,mul(back,lead)),low,end]
                    for k,(v,w) in enumerate(zip(points,points[1:])):
                        key=f'transition.{index}.well.{k}'
                        self.edges.append((key,v,w,pf,False))
                        if k==3:self.vertical_axes[key]=unit([back[1],-back[0],0])
                    consumed.add(j)
                    continue
            if not candidates:raise ValueError("平台扶手缺少同高可连接端点")
            b=min(candidates,key=lambda v:math.dist(a,v));available.remove(b)
            if upper:
                lower=[a[0],a[1],b[2]]
                key=f'transition.{index}.rise'
                self.edges.append((key,a,lower,pf,False))
                self.vertical_axes[key]=unit([d[1],-d[0],0])
                a=lower
            middle=add(a,mul(d,dot(sub(b,a),d)))
            # Avoid a tiny double-miter offcut: extend the receiving rail end
            # along its own axis to the connector, without moving its posts.
            if 1e-6<math.dist(middle,b)<2*max(pf.width,pf.depth):
                for ei,(ek,ea,eb,ep,platform) in enumerate(self.edges):
                    if not platform:continue
                    at_a=ident(ea)==ident(b);at_b=ident(eb)==ident(b)
                    if not (at_a or at_b):continue
                    direction=unit(sub(eb,ea))
                    delta=sub(middle,b)
                    if abs(dot(unit(delta),direction))>1-1e-7:
                        self.edges[ei]=(ek,middle if at_a else ea,middle if at_b else eb,ep,platform)
                        b=middle
                    break
            points=[a,middle,b]
            points=[v for i,v in enumerate(points) if i==0 or math.dist(v,points[i-1])>1e-6]
            for j,(v,w) in enumerate(zip(points,points[1:])):
                self.edges.append((f"transition.{index}.{j}",v,w,pf,False))
        adjacent={}
        for i,(_,a,b,_,_) in enumerate(self.edges):
            adjacent.setdefault(ident(a),[]).append((i,unit(sub(b,a))))
            adjacent.setdefault(ident(b),[]).append((i,unit(sub(a,b))))
        envelopes=[]
        for i,(key,a,b,pf,_) in enumerate(self.edges):
            d=unit(sub(b,a));x=self.vertical_axes[key] if key in self.vertical_axes else unit([-d[1],d[0],0]);y=cross(d,x)
            pad=4*max(pf.width,pf.depth)
            overlap=max(pf.width,pf.depth)/2
            display_start=sub(a,mul(d,overlap)) if len(adjacent[ident(a)])>1 else a
            display_end=add(b,mul(d,overlap)) if len(adjacent[ident(b)])>1 else b
            display=tube(key+'.display_stock',display_start,display_end,pf,x,y)
            raw=tube(key+'.stock',sub(a,mul(d,pad)),add(b,mul(d,pad)),pf,x,y)
            outer=tube(key+'.network_envelope',sub(a,mul(d,pad)),add(b,mul(d,pad)),pf,x,y,outer=True)
            ops=[]
            for end,(v,inward) in enumerate(((a,d),(b,mul(d,-1)))):
                neighbors=[w for j,w in adjacent[ident(v)] if j!=i]
                if len(neighbors)>1:raise ValueError("扶手连续链不能含未定义的三通节点")
                n=unit(sub(inward,neighbors[0])) if neighbors else inward
                # Retain the half-space pointing into this member.
                u=unit(cross(n,[0,0,1] if abs(n[2])<.9 else [0,1,0]));w=cross(n,u)
                extent=4*(math.dist(a,b)+pad)
                keep=prism(key+f'.end.{end}',[[-extent,-extent],[extent,-extent],[extent,extent],[-extent,extent]],v,u,w,mul(n,extent))
                raw=boolean(key+f'.cut.{end}',raw,[keep],'intersect')
                outer=boolean(key+f'.envelope.cut.{end}',outer,[keep],'intersect')
                ops.append({'kind':'miter' if neighbors else 'square','point':v,'inwardNormal':n})
            # Axial blank includes the extreme projection of each oblique cut.
            extra=sum((pf.width*abs(dot(op['inwardNormal'],x))+pf.depth*abs(dot(op['inwardNormal'],y)))/(2*abs(dot(op['inwardNormal'],d))) for op in ops)
            item(key,'连续扶手转接' if key.startswith('transition.') else '扶手',raw,'tube',[math.dist(a,b)+extra],pf,ops,display_solid=display)
            envelopes.append((outer,a,b,pf))
        for index,(point,members) in enumerate(adjacent.items()):
            if len(members)==2:
                relationship(f'handrail.joint.{index}','weld',[self.edges[i][0] for i,_ in members],
                             properties={'geometry':'bisector-miter','point':list(point)})
        return envelopes
