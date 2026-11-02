"""Audit physical guide engagement against the shipped URDF and STL geometry."""
import json, math, pathlib, struct, xml.etree.ElementTree as ET
root=pathlib.Path(__file__).resolve().parents[4]
asset=root/'src/apps/five-axis-cam/resources/machines/industrial-ac'
urdf=ET.parse(asset/'LC-3015.urdf').getroot()
joints={j.get('name'):j for j in urdf.findall('joint')}
def limits(name):
    j=joints[name];s=1000 if j.get('type')=='prismatic' else 180/math.pi
    return [float(j.find('limit').get(k))*s for k in ('lower','upper')]
def bounds(name):
    b=(asset/'meshes'/name).read_bytes();n=struct.unpack_from('<I',b,80)[0];points=[]
    for i in range(n):
        v=struct.unpack_from('<12fH',b,84+50*i)
        points.extend([[v[k+a]*1000 for a in range(3)] for k in (3,6,9)])
    return [[min(p[a] for p in points),max(p[a] for p in points)] for a in range(3)]
xrail=bounds('base_rail.stl')[0];xblock=bounds('x_bridge_teal.stl')[0]
xorigin=float(joints['X'].find('origin').get('xyz').split()[0])*1000
xmargins=[xorigin+limits('X')[0]+xblock[0]-xrail[0],xrail[1]-(xorigin+limits('X')[1]+xblock[1])]
yrail=bounds('x_bridge_rail.stl')[1];ysaddle=bounds('y_carriage_dark.stl')[1]
ymargins=[limits('Y')[0]+ysaddle[0]-yrail[0],yrail[1]-(limits('Y')[1]+ysaddle[1])]
zrail=bounds('z_slide_rail.stl')[2];zguide=bounds('y_carriage_rail.stl')[2]
zorigin=float(joints['Z'].find('origin').get('xyz').split()[2])*1000
zmargins=[zorigin+limits('Z')[0]+zrail[1]-zguide[1],zguide[0]-(zorigin+limits('Z')[1]+zrail[0])]
assert min(xmargins)>200,xmargins
assert min(ymargins)>100,ymargins
assert min(zmargins)>100,zmargins
assert abs(xorigin+float(joints['Z'].find('origin').get('xyz').split()[0])*1000)<1e-7
assert all(link.findall('collision') for link in urdf.findall('link'))
assert all(float(j.find('limit').get('velocity'))>0 for j in joints.values() if j.get('type')!='fixed')
report={'limits':{k:limits(k) for k in ['X','Y','Z','C','A']},'guideMarginsMm':{'X':xmargins,'Y':ymargins,'Z':zmargins},'toolXAtZeroMm':0,'collisionLinks':len(urdf.findall('link'))}
out=root/'output/tests/machine-simulation/physical-travel.json';out.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
