"""Exact directional section masks; no product IDs or planar-wall assumptions."""
import copy
import math
import importlib.util
from pathlib import Path


def _module(name):
    path = Path(__file__).with_name(name + '.py')
    spec = importlib.util.spec_from_file_location('directional_' + name, path)
    module = importlib.util.module_from_spec(spec)
    # The profile catalog declares dataclasses, whose module must be registered.
    import sys
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def shell(profile):
    """Read a closed hollow section without imposing planar support walls."""
    geometry = _module('section_geometry')
    parsed = geometry.from_profile(profile)
    loops = parsed['contours']
    if len(loops) != 2 or not all(c['closed'] for c in loops):
        raise ValueError('公母插接需要一个闭合外轮廓和一个闭合内轮廓')
    outer, inner = [geometry.bounds(c) for c in loops]
    gaps = [inner['min'][i]-outer['min'][i] for i in (0,1)]
    gaps += [outer['max'][i]-inner['max'][i] for i in (0,1)]
    if min(gaps) <= parsed['tolerance']:
        raise ValueError('公母插接内轮廓必须位于外轮廓内')
    return {'outside':outer, 'inside':inner, 'wallThickness':min(gaps),
            'contours':loops, 'tolerance':parsed['tolerance']}


def expanded_ring(contours, clearance):
    if len(contours) != 2 or clearance < 0 or not math.isfinite(clearance):
        raise ValueError('母口截面或间隙无效')
    if clearance == 0:
        return copy.deepcopy(contours)
    offset = _module('tube_profile_catalog')._offset_outer_contour
    return [offset(contours[0], clearance), offset(contours[1], -clearance)]


def cross(a,b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def flat_receiver_face(shell,face,bounds):
    """Explicit planar-contact query, reserved for a full planar socket fit."""
    geometry=_module('section_geometry')
    lo,hi=bounds['min'],bounds['max'];outer,inner=shell['outside'],shell['inside']
    ni,ai=(2,1) if face in ('top','bottom') else (1,2)
    spans=[outer['max'][i]-outer['min'][i] for i in (0,1)]
    actual=[hi[i]-lo[i] for i in (1,2)]
    axes=(0,1) if all(abs(a-b)<.05 for a,b in zip(spans,actual)) else (1,0)
    normal,across=axes[ni-1],axes[ai-1]
    side='max' if face in ('top','right') else 'min'
    ranges=[]
    for loop,box in zip(shell['contours'],(outer,inner)):
        at=box[side][normal]
        edges=[e for e in loop['edges'] if e['kind']=='line'
               and abs(e['start'][normal]-at)<shell['tolerance']
               and abs(e['end'][normal]-at)<shell['tolerance']]
        if len(edges)!=1:raise ValueError('当前直接插入需要唯一的平直承接壁')
        ranges.append(sorted((edges[0]['start'][across],edges[0]['end'][across])))
    center=(lo[ai]+hi[ai])/2
    shift=center-(outer['min'][across]+outer['max'][across])/2
    return {'center':center,'wall':abs(outer[side][normal]-inner[side][normal]),
            'flat':[max(r[0] for r in ranges)+shift,min(r[1] for r in ranges)+shift]}


def frame_axes(frame, outward, end):
    """Use source-verified branch axes, including the actual inserted end."""
    if frame.get('verification') != 'committed-source-brep-replay':
        raise ValueError('公母插舌缺少已核对的真实制造截面')
    axes = [frame.get(k) for k in ('xAxisToolPart','yAxisToolPart','branchManufacturingZHostToolPart')]
    for axis in axes:
        if (not isinstance(axis,list) or len(axis)!=3 or any(isinstance(v,bool)
                or not isinstance(v,(int,float)) or not math.isfinite(v) for v in axis)
                or abs(sum(v*v for v in axis)-1)>1e-6):
            raise ValueError('公母插舌真实截面方向无效')
    u,v,z = axes
    axis = cross(u,v)
    if abs(sum(a*b for a,b in zip(u,v)))>1e-6 or abs(sum(a*b for a,b in zip(z,axis)))>1e-6:
        raise ValueError('公母插舌制造方向须位于真实截面内')
    if end not in ('start','end'):
        raise ValueError('公母插舌须指定真实支管端向')
    x = [a*(1 if end=='start' else -1) for a in outward]
    if abs(sum(a*b for a,b in zip(axis,x)))<1-1e-6:
        raise ValueError('公母插舌支管轴线须沿主管接触法向')
    return z,cross(z,x)


def rectangle(width,height):
    p=[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]]
    return {'kind':'path','closed':True,'segments':[
        {'kind':'line','start':a,'end':b} for a,b in zip(p,p[1:]+p[:1])]}


def angular_bands(count,angle,z_axis=(0.,0.,1.),y_axis=(0.,1.,0.)):
    if count not in (2,4):raise ValueError('插舌方位数量只能为 2 或 4')
    if not isinstance(angle,(int,float)) or isinstance(angle,bool) or not math.isfinite(angle):
        raise ValueError('插舌周向角必须为有限数值')
    for phase in ([angle] if count==2 else [angle,angle+90.]):
        c,s=math.cos(math.radians(phase)),math.sin(math.radians(phase))
        yield ([c*z_axis[i]+s*y_axis[i] for i in range(3)],
               [c*y_axis[i]-s*z_axis[i] for i in range(3)])


class Graph:
    def __init__(self):self.nodes=[]

    def prism(self,key,contours,origin,x,y,vector):
        self.nodes.extend([
            {'key':key+'.profile','operator':'profile2d','arguments':{
                'placement':{'origin':list(origin),'xAxis':list(x),'yAxis':list(y)},
                'contours':copy.deepcopy(contours)}},
            {'key':key,'operator':'extrude','inputs':[key+'.profile'],
             'arguments':{'vector':list(vector)}}])
        return key

    def boolean(self,key,operation,inputs):
        if len(inputs)==1 and operation=='union':
            return inputs[0]
        self.nodes.append({'key':key,'operator':'boolean','inputs':inputs,
                           'arguments':{'operation':operation}})
        return key


def end_cutter(bounds,tip_contour,depth,count,angle,end):
    """Leave the actual stock only within angular tongue prisms at its end."""
    lo,hi=bounds['min'],bounds['max']
    reach=4*(sum(hi[i]-lo[i] for i in range(3))+depth+10.)
    mirror=-1. if end=='end' else 1.
    center=[0.,(lo[1]+hi[1])/2,mirror*(lo[2]+hi[2])/2]
    g=Graph()
    slab=g.prism('end-slab',[rectangle(depth+reach,2*reach)],[(depth-reach)/2,0.,-reach],
                 [1.,0.,0.],[0.,1.,0.],[0.,0.,2*reach])
    tabs=[]
    for i,(normal,tangent) in enumerate(angular_bands(count,angle,
                                                     (0.,0.,mirror),(0.,1.,0.))):
        origin=[center[j]-normal[j]*reach for j in range(3)]
        tabs.append(g.prism('tab-band.'+str(i),[tip_contour],origin,[1.,0.,0.],tangent,
                            [value*2*reach for value in normal]))
    ears=g.boolean('end-tabs','union',tabs)
    g.boolean('tool','subtract',[slab,ears])
    return g.nodes


def socket_cutter(contours,section_origin,u,v,center,inward,depth,width,count,angle,z_axis,y_axis,reach):
    """Cut the real mating wall ring clipped by angular bands, in a one-way sweep."""
    g=Graph()
    ring=g.prism('mating-ring',contours,section_origin,u,v,[a*depth for a in inward])
    masks=[]
    for i,(normal,tangent) in enumerate(angular_bands(count,angle,z_axis,y_axis)):
        masks.append(g.prism('socket-band.'+str(i),[rectangle(width,2*reach)],center,
                              tangent,normal,[a*depth for a in inward]))
    region=g.boolean('socket-bands','union',masks)
    g.boolean('tool','intersect',[ring,region])
    return g.nodes
