"""Match directional tongues with exact, near-half-shell mother openings."""
import importlib.util
import math
from pathlib import Path

_path = (Path(section_geometry.__file__).with_name('directional_tube_geometry.py')
         if 'section_geometry' in globals() else
         Path(__file__).resolve().parents[2] / '_shared' / 'directional_tube_geometry.py')
_spec = importlib.util.spec_from_file_location('paired_socket_directions', _path)
directions = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(directions)
EPS = .01


def _number(value,label):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):
        raise ValueError(label+'必须为有限数值')
    return value


def generate(parameters, context):
    host = directions.shell(context.get('targetSection'))
    feature = context['feature']
    profile = feature.get('section',{}).get('profile')
    branch = directions.shell(profile)
    lo,hi = context['bounds']['min'], context['bounds']['max']
    face = feature.get('face','top')
    if face not in ('top','bottom','left','right'):
        raise ValueError('母槽须指定主管加工方位')
    ni,ai = (2,1) if face in ('top','bottom') else (1,2)
    sign = 1. if face in ('top','right') else -1.
    outward=[0.,0.,0.];outward[ni]=sign
    across=[0.,0.,0.];across[ai]=1.
    along=[1.,0.,0.]
    insertion=parameters['tabLength']
    if insertion<=host['wallThickness']+EPS:
        raise ValueError('插舌长度必须穿过承接管进入侧壁')
    if insertion>=hi[ni]-lo[ni]-host['wallThickness']-EPS:
        raise ValueError('插舌长度会碰到承接管对侧壁')
    spans=[host['outside']['max'][i]-host['outside']['min'][i] for i in (0,1)]
    actual=[hi[i]-lo[i] for i in (1,2)]
    if not any(all(abs(a-b)<=max(.05,host['tolerance']*10) for a,b in zip(candidate,actual))
               for candidate in (spans,list(reversed(spans)))):
        raise ValueError('实际承接管横截面与母槽保存截面不一致')
    station = _number(feature.get('station',0),'母槽轴向位置')
    reference=feature.get('reference','start')
    if reference=='start':axial=lo[0]+station
    elif reference=='end':axial=hi[0]-station
    elif reference=='center':axial=(lo[0]+hi[0])/2+station
    else:raise ValueError('母槽定位基准无效')
    offset=_number(feature.get('offset',0),'母槽横向偏移')
    center=[axial,0.,0.]
    center[ai]=(lo[ai]+hi[ai])/2+offset
    center[ni]=(hi[ni] if sign>0 else lo[ni])+sign*EPS
    frame=profile.get('sectionFrame')
    if frame is not None:
        u,v=frame['xAxisToolPart'],frame['yAxisToolPart']
        z,y=directions.frame_axes(frame,outward,feature['section'].get('branchEnd'))
    elif feature['section'].get('directionFrame') is not None:
        declared=feature['section']['directionFrame']
        u,v,z,y=[declared[k] for k in ('uAxis','vAxis','zAxis','yAxis')]
        for axis in (u,v,z,y):
            if len(axis)!=3 or any(not math.isfinite(_number(a,'截面方向')) for a in axis) or abs(sum(a*a for a in axis)-1)>1.e-6:
                raise ValueError('示例构件的截面方向无效')
        if any(abs(sum(a*b for a,b in zip(first,second)))>1.e-6 for first,second in ((u,v),(z,y),(u,outward),(v,outward))):
            raise ValueError('示例构件的截面方向须正交并垂直接入主管')
    else:
        rotation=context.get('placement',{}).get('rotation',0)
        rotation=math.radians(_number(rotation,'母槽截面转角'))
        c,s=math.cos(rotation),math.sin(rotation)
        first=[c*along[i]+s*across[i] for i in range(3)]
        second=[-s*along[i]+c*across[i] for i in range(3)]
        # pairRotation selects a declared section pose. The along-host pair
        # uses the branch's end end, whose physical Z points along +host X.
        # Combining it with an ordinary rotation would mirror a nonzero phase.
        if parameters.get('pairRotation',0)%180==90:z,y=first,second
        elif parameters.get('pairRotation',0)%180==0:z,y=second,first
        else:raise ValueError('母槽截面方位只能为横向或轴向')
        u,v=y,z
    # A ring follows the inserted tube's actual wall, including curved walls.
    # The band selects the same angular directions as the retained male tabs.
    clearance=parameters['sideClearance']
    contours=directions.expanded_ring(profile['contours'],clearance)
    reach=4*(sum(hi[i]-lo[i] for i in range(3))+100.)
    nodes=directions.socket_cutter(contours,center,u,v,center,[-a for a in outward],
        (hi[ni]-lo[ni])/2+2*EPS,parameters['tabWidth']+2*clearance,
        parameters['pairCount'],parameters.get('tabAngle',0.),z,y,reach)
    return {'mode':'solid','coordinateSpace':'part','outputKey':'tool',
            'model':{'schema':'icax.neutral-model','schemaVersion':1,
                     'template':{'id':'paired-side-slots','version':'3.0.0','packageDigest':'self-contained'},
                     'geometry':nodes}}
