"""Keep two or four circumferential tongues in the actual tube shell."""
import importlib.util
import math
from pathlib import Path

_path = (Path(section_geometry.__file__).with_name('directional_tube_geometry.py')
         if 'section_geometry' in globals() else
         Path(__file__).resolve().parents[2] / '_shared' / 'directional_tube_geometry.py')
_spec = importlib.util.spec_from_file_location('paired_end_directions', _path)
directions = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(directions)
EPS = 0.01


def _round_tab(depth, width):
    radius = width / 2
    if depth <= radius:
        raise ValueError("圆弧插舌长度须大于其半宽")
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": [depth + EPS, -radius], "end": [radius, -radius]},
        {"kind": "arc", "start": [radius, -radius], "middle": [0, 0], "end": [radius, radius]},
        {"kind": "line", "start": [radius, radius], "end": [depth + EPS, radius]},
        {"kind": "line", "start": [depth + EPS, radius], "end": [depth + EPS, -radius]},
    ]}


def generate(parameters, context):
    own = directions.shell(context.get('targetSection'))
    partner = directions.shell(context.get('section', {}).get('profile'))
    width, depth = parameters['tabWidth'], parameters['tabLength']
    count = parameters['pairCount']
    if depth <= width/2 + EPS:
        raise ValueError('圆弧插舌长度须大于半宽')
    if depth <= partner['wallThickness'] + EPS:
        raise ValueError('插舌长度必须穿过承接管进入侧壁')
    if depth >= partner['outside']['max'][1]-partner['inside']['min'][1]-EPS:
        raise ValueError('插舌长度会碰到承接管对侧壁')
    bounds, place = context['bounds'], context['placement']
    spans = [own['outside']['max'][i]-own['outside']['min'][i] for i in (0,1)]
    actual = [bounds['max'][i]-bounds['min'][i] for i in (1,2)]
    if not any(all(abs(a-b)<=max(.05,own['tolerance']*10) for a,b in zip(candidate,actual))
               for candidate in (spans,list(reversed(spans)))):
        raise ValueError('实际管材横截面与插舌保存截面不一致')
    length = bounds['max'][0]-bounds['min'][0]
    if not math.isfinite(place['trim']) or place['trim']<0 or place['trim']+depth>=length-EPS:
        raise ValueError('端部修剪量和插舌长度须小于管材长度')
    # Native placement owns operation.rotation. tabAngle describes the shared
    # physical circumferential phase of both mating cutters.
    nodes = directions.end_cutter(bounds, _round_tab(depth,width), depth, count,
                                  parameters.get('tabAngle',0.), context['end'])
    return {'mode':'solid','coordinateSpace':'end-local','outputKey':'tool',
            'datumCenter':None,'requireEndContact':True,'requireRetainedEndMaterial':True,
            'model':{'schema':'icax.neutral-model','schemaVersion':1,
                     'template':{'id':'paired-end-tabs','version':'3.0.0','packageDigest':'self-contained'},
                     'geometry':nodes}}
