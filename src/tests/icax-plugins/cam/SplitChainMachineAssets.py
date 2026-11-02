"""Independent authored-asset contract checks, in mm and radians.

STL vertices and face centres must be covered by the declared box union within
0.01 mm. The runtime independently checks swept mesh/box separation.
"""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import numpy as np
from IndustrialMachineSurface import triangles

ROOT = Path(__file__).resolve().parents[4]
SOURCES = [('rotary-four-plus-one', 'RC-1000.urdf', {'X', 'Y', 'Z', 'B'}, {'C'}),
           ('cradle-three-plus-two', 'TC-800.urdf', {'X', 'Y', 'Z'}, {'A', 'C'})]

def values(node, key, default):
    return np.array([float(v) for v in node.get(key, default).split()])

def rotation(rpy):
    x, y, z = rpy
    sx,cx,sy,cy,sz,cz = np.sin(x),np.cos(x),np.sin(y),np.cos(y),np.sin(z),np.cos(z)
    return np.array([[cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx],
                     [sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx],[-sy,cy*sx,cy*cx]])

def origin(node):
    if node is None:
        return np.eye(4)
    result=np.eye(4)
    result[:3,:3]=rotation(values(node,'rpy','0 0 0'))
    result[:3,3]=values(node,'xyz','0 0 0')*1000
    return result

def surface_distance(point, faces):
    a,b,c=faces[:,0],faces[:,1],faces[:,2]
    ab,ac=b-a,c-a
    normal=np.cross(ab,ac);normal2=(normal*normal).sum(axis=1)
    height=((point-a)*normal).sum(axis=1)/np.maximum(normal2,1e-30)
    projection=point-height[:,None]*normal
    ap=projection-a
    d00=(ab*ab).sum(axis=1);d01=(ab*ac).sum(axis=1);d11=(ac*ac).sum(axis=1)
    d20=(ap*ab).sum(axis=1);d21=(ap*ac).sum(axis=1)
    denominator=np.maximum(d00*d11-d01*d01,1e-30)
    u=(d11*d20-d01*d21)/denominator;v=(d00*d21-d01*d20)/denominator
    inside=(u>=-1e-9)&(v>=-1e-9)&(u+v<=1+1e-9)&(normal2>1e-20)
    distances=np.where(inside,np.linalg.norm(point-projection,axis=1),np.inf)
    for start,end in [(a,b),(b,c),(c,a)]:
        edge=end-start
        t=np.clip(((point-start)*edge).sum(axis=1)/np.maximum((edge*edge).sum(axis=1),1e-30),0,1)
        distances=np.minimum(distances,np.linalg.norm(point-(start+t[:,None]*edge),axis=1))
    return float(distances.min())

def static_chuck_contract(folder, links, joints):
    if folder.name!='rotary-four-plus-one':return None
    assert len(links)==11
    assert all(link.find('icax_radial_chuck') is None and link.find('icax_chuck_jaw') is None for link in links.values())
    owner=next(joint.find('child').get('link') for joint in joints.values() if joint.get('name')=='C')
    support=next(name for name,link in links.items() if link.find('icax_work_support') is not None)
    def faces_of(link):
        result=[]
        for node in link.findall('visual'):
            mesh=node.find('geometry/mesh');matrix=origin(node.find('origin'))
            data=triangles(folder/mesh.get('filename'))*values(mesh,'scale','1 1 1')
            result.append(data@matrix[:3,:3].T+matrix[:3,3])
        return np.concatenate(result)
    def zero_world(name):
        if name not in joints:return np.eye(4)
        joint=joints[name]
        return zero_world(joint.find('parent').get('link'))@origin(joint.find('origin'))
    owner_world=zero_world(owner);support_world=zero_world(support)
    parent_faces=faces_of(links[owner]);parent_world=parent_faces@owner_world[:3,:3].T+owner_world[:3,3]
    high_points=parent_world.reshape(-1,3);high_points=high_points[high_points[:,2]>48]
    rows=[]
    for index,angle in enumerate([0,2*np.pi/3,4*np.pi/3]):
        direction=np.array([np.cos(angle),np.sin(angle),0])
        radial=high_points@direction;inner=float(radial[radial>0].min())
        assert abs(inner-130)<.001
        collider=links[owner].findall('collision')[index+1]
        matrix=owner_world@origin(collider.find('origin'))
        half=values(collider.find('geometry/box'),'size','0 0 0')*500
        inner_box=float(matrix[:3,3]@direction-np.abs(matrix[:3,:3].T@direction)@half)
        assert abs(inner_box-130)<.001
        rows.append({'jaw':index+1,'openingDiameterMm':2*inner,'colliderOpeningDiameterMm':2*inner_box,
                     'radialGapTo202CylinderMm':inner-101})
    faces=faces_of(links[support]);world=faces@support_world[:3,:3].T+support_world[:3,3]
    assert np.linalg.norm((faces.reshape(-1,3).min(axis=0)+faces.reshape(-1,3).max(axis=0))/2)<.001
    assert abs(world[:,:,2].min()+8)<.001 and abs(world[:,:,2].max())<.001
    assert abs(np.linalg.norm(world[:,:,:2],axis=2).max()-260)<.001
    top=world[np.all(np.abs(world[:,:,2])<.001,axis=1)]
    top_area=np.linalg.norm(np.cross(top[:,1]-top[:,0],top[:,2]-top[:,0]),axis=1).sum()/2
    expected_area=64*.5*260**2*np.sin(2*np.pi/64)
    assert abs(top_area-expected_area)<.1, 'Flat support must include centre and annulus without a gap or duplicate area'
    duplicate_top=parent_world[np.all(np.abs(parent_world[:,:,2])<.001,axis=1)]
    assert len(duplicate_top)==0, 'The support annulus must not remain on the parent body'
    collider=links[support].find('collision')
    assert np.allclose(values(collider.find('geometry/box'),'size','0 0 0')*1000,[520,520,8])
    return {'owner':owner,'support':support,'openDiameterMm':260.,'supportDiameterMm':520.,
            'supportWorldTopZMm':float(world[:,:,2].max()),'supportTopAreaMm2':float(top_area),'jaws':rows,'pass':True}

def check(folder, filename, tool_axes, support_axes):
    document=ET.parse(folder/filename).getroot()
    links={link.get('name'):link for link in document.findall('link')}
    joints={joint.find('child').get('link'):joint for joint in document.findall('joint')}
    roots=set(links)-set(joints)
    assert len(roots)==1
    active={joint.get('name'):joint for joint in joints.values() if joint.get('type')!='fixed'}
    assert set(active)==tool_axes|support_axes
    for name,joint in active.items():
        axis=values(joint.find('axis'),'xyz','0 0 0')
        assert abs(np.linalg.norm(axis)-1)<1e-9, name
        limit=joint.find('limit');lo,hi=float(limit.get('lower')),float(limit.get('upper'))
        home=joint.find('icax_home');q=float(home.get('position')) if home is not None else 0
        assert np.isfinite([lo,hi,q]).all() and lo<=q<=hi and lo<hi, name
    tool=[name for name,link in links.items() if link.find('icax_tool') is not None]
    support=[name for name,link in links.items() if link.find('icax_work_support') is not None]
    assert len(tool)==len(support)==1
    def ancestors(name):
        axes=set();seen=set()
        while name in joints:
            assert name not in seen
            seen.add(name);joint=joints[name]
            if joint.get('type')!='fixed':axes.add(joint.get('name'))
            name=joint.find('parent').get('link')
        return axes
    assert ancestors(tool[0])==tool_axes
    assert ancestors(support[0])==support_axes
    endpoint=links[tool[0]].find('icax_tool')
    tcp=values(endpoint,'tcp_mm','nan nan nan')
    beam=values(endpoint,'beam','0 0 0')
    assert np.isfinite(tcp).all() and abs(np.linalg.norm(beam)-1)<1e-9
    rows=[]
    for name,link in links.items():
        visual=[]
        for node in link.findall('visual'):
            mesh=node.find('geometry/mesh')
            assert mesh is not None
            data=triangles(folder/mesh.get('filename'))
            scale=values(mesh,'scale','1 1 1')
            transform=origin(node.find('origin'))
            data=(data*scale)@transform[:3,:3].T+transform[:3,3]
            visual.append(data)
        faces=np.concatenate(visual)
        samples=np.concatenate([faces.reshape(-1,3),faces.mean(axis=1)])
        covered=np.zeros(len(samples),dtype=bool)
        colliders=link.findall('collision')
        assert colliders, name
        for collider in colliders:
            box=collider.find('geometry/box');assert box is not None
            half=values(box,'size','0 0 0')*500
            assert (half>0).all()
            transform=origin(collider.find('origin'))
            local=(samples-transform[:3,3])@transform[:3,:3]
            covered|=(np.abs(local)<=half+.01).all(axis=1)
        uncovered=samples[~covered]
        assert not len(uncovered), f'{folder.name}/{name}: {len(uncovered)} uncovered samples, examples {uncovered[:4]}'
        rows.append({'link':name,'triangles':len(faces),'colliders':len(colliders),'uncoveredSamples':len(uncovered)})
        if name==tool[0]:
            # Authored physical TCP must be a visual surface point, not an
            # undeclared stand-off from the nozzle used twice by CAM.
            tipDistance=surface_distance(tcp,faces)
            assert tipDistance<.01, f'TCP is {tipDistance} mm from the physical nozzle surface'
    result={'model':folder.name,'tool':tool[0],'support':support[0],
            'toolAxes':sorted(tool_axes),'supportAxes':sorted(support_axes),
            'tcpMm':tcp.tolist(),'beam':beam.tolist(),'links':rows,'pass':True}
    chuck=static_chuck_contract(folder,links,joints)
    if chuck:result['staticChuck']=chuck
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=ROOT/'src/apps/five-axis-cam/resources/machines');parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    results=[check(args.root/folder,filename,ta,sa) for folder,filename,ta,sa in SOURCES]
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'pass':True,'models':len(results),'links':sum(len(r['links']) for r in results)}))
