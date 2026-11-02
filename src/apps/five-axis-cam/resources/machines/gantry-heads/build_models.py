"""Build five gantry layouts from the shipped authored rigid bodies.

No downloaded geometry or run-time Python dependency. All transforms affect
visuals, collision boxes, joints and physical TCP together. URDF/STL use metres.
The separate head catalog is also generated here; machine and head share links.
"""
from pathlib import Path
import copy
import json
import math
import shutil
import struct
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
MACHINES = ROOT.parent
SPECS = [
    ('GAC-3015', 'AC', 'AC 双回转头', 'industrial-ac/LC-3015.urdf'),
    ('GAB-3015', 'AB', 'AB 双摆头', 'industrial-ac/LC-3015.urdf'),
    ('GBC-3015', 'BC', 'BC 双回转头', 'industrial-ac/LC-3015.urdf'),
    ('GA-3015', '4+1', '4+1 A 单摆头', 'rotary-four-plus-one/RC-1000.urdf'),
    ('GF-3015', '3+2', '3+2 固定头', 'cradle-three-plus-two/TC-800.urdf'),
]


def numbers(text):
    return [float(v) for v in text.split()]


def vector(values):
    return ' '.join(format(v, '.15g') for v in values)


def rotate_z(point, angle):
    x, y, z = point
    return [math.cos(angle)*x-math.sin(angle)*y,
            math.sin(angle)*x+math.cos(angle)*y, z]


def origin(node):
    result = node.find('origin')
    if result is None:
        result = ET.SubElement(node, 'origin')
    return result


def rotate_link_z(link, angle):
    # Rotating a local body also rotates its calibration. Joint frames are
    # kept in the machine XYZ basis; the physical axis is set separately.
    for attachment in link.findall('visual')+link.findall('collision'):
        o = origin(attachment)
        o.set('xyz', vector(rotate_z(numbers(o.get('xyz', '0 0 0')), angle)))
        rpy = numbers(o.get('rpy', '0 0 0'))
        rpy[2] += angle
        o.set('rpy', vector(rpy))
    tool = link.find('icax_tool')
    if tool is not None:
        tool.set('tcp_mm', vector(rotate_z(numbers(tool.get('tcp_mm')), angle)))
        tool.set('beam', vector(rotate_z(numbers(tool.get('beam')), angle)))


def rename(robot, old, new):
    robot.find(f"link[@name='{old}']").set('name', new)
    for joint in robot.findall('joint'):
        for tag in ('parent', 'child'):
            if joint.find(tag).get('link') == old:
                joint.find(tag).set('link', new)


def copy_meshes(robot, source, output, prefix):
    for mesh in robot.findall('.//mesh'):
        path = Path(mesh.get('filename'))
        target = output/'meshes'/(prefix+'_'+path.name)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source.parent/path, target)
        mesh.set('filename', 'meshes/'+target.name)


def write(robot, output, name):
    ET.indent(robot)
    ET.ElementTree(robot).write(output/name, encoding='utf-8', xml_declaration=True)


def write_triangles(path,triangles):
    with path.open('wb') as stream:
        stream.write(b'iCAX authored gantry head structure'.ljust(80,b'\0'));stream.write(struct.pack('<I',len(triangles)))
        for a,b,c in triangles:
            u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
            n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];length=math.sqrt(sum(x*x for x in n))
            assert length>1e-16
            stream.write(struct.pack('<12fH',*(x/length for x in n),*(v for p in (a,b,c) for v in p),0))


def add_pedestal(base, output, center, size, name):
    """An authored welded stand, attached to the stationary bed."""
    points = [tuple(center[k]+size[k]*(.5 if bits & (1 << k) else -.5) for k in range(3)) for bits in range(8)]
    faces = [(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)]
    triangles = [tuple(points[i] for i in tri) for a,b,c,d in faces for tri in [(a,b,c),(a,c,d)]]
    path = output/'meshes'/(name+'.stl')
    write_triangles(path,triangles)
    visual = ET.SubElement(base, 'visual', name=name)
    ET.SubElement(ET.SubElement(visual,'geometry'),'mesh',filename='meshes/'+path.name)
    ET.SubElement(ET.SubElement(visual,'material',name='stand'),'color',rgba='0.54 0.61 0.65 1')
    collision = ET.SubElement(base,'collision',name=name+'_box')
    ET.SubElement(collision,'origin',xyz=vector(center))
    ET.SubElement(ET.SubElement(collision,'geometry'),'box',size=vector(size))


def ab_yoke(robot,output):
    """Compact side-drive AB head, with independently carried physical bodies.

    The flange, enclosed shoulder and side motor follow the public TKF420
    arrangement. This is an authored generic model, not vendor CAD/calibration.
    """
    link=robot.find("link[@name='B 回转叉架']")
    for child in list(link):link.remove(child)
    mount=ET.SubElement(robot,'link',name='AB 刀头安装座')
    joint=robot.find("joint[@name='B']")
    attachment=ET.SubElement(robot,'joint',name='ab_head_mount',type='fixed')
    ET.SubElement(attachment,'parent',link=joint.find('parent').get('link'))
    ET.SubElement(attachment,'child',link=mount.get('name'))
    attachment.append(copy.deepcopy(joint.find('origin')))
    joint.find('parent').set('link',mount.get('name'))
    origin(joint).set('xyz','0 0 0')
    # The stationary motor is tucked behind the tool instead of hanging from
    # an oversized open frame. Trim only the actual occupied mounting space.
    ram=robot.find("link[@name='Z 滑枕']")
    top=.8055;bottom=-.805;factor=1-.08/(top-bottom)
    changed=set()
    for visual in ram.findall('visual'):
        o=visual.find('origin');assert o is None or numbers(o.get('rpy','0 0 0'))==[0,0,0]
        offset=0 if o is None else numbers(o.get('xyz','0 0 0'))[2]
        path=output/visual.find('geometry/mesh').get('filename')
        if path in changed:continue
        changed.add(path);triangles=[]
        for row in struct.iter_unpack('<12fH',path.read_bytes()[84:]):
            triangles.append(tuple((row[k],row[k+1],top+(row[k+2]+offset-top)*factor-offset) for k in (3,6,9)))
        write_triangles(path,triangles)
    for collision in ram.findall('collision'):
        o=origin(collision);p=numbers(o.get('xyz','0 0 0'));assert numbers(o.get('rpy','0 0 0'))==[0,0,0]
        p[2]=top+(p[2]-top)*factor;o.set('xyz',vector(p))
        box=collision.find('geometry/box');size=numbers(box.get('size'));size[2]*=factor;box.set('size',vector(size))
    steel='0.52 0.57 0.63 1';shell='0.13 0.17 0.21 1';dark='0.055 0.07 0.085 1'
    def part(body,name,center,size,triangles,color):
        path=output/'meshes'/(name+'.stl');write_triangles(path,triangles)
        visual=ET.SubElement(body,'visual',name=name)
        ET.SubElement(ET.SubElement(visual,'geometry'),'mesh',filename='meshes/'+path.name)
        ET.SubElement(ET.SubElement(visual,'material',name=name+'_finish'),'color',rgba=color)
        collision=ET.SubElement(body,'collision',name=name+'_box');ET.SubElement(collision,'origin',xyz=vector(center))
        ET.SubElement(ET.SubElement(collision,'geometry'),'box',size=vector(size))
    def extrude(body,name,center,size,axis,corner,color):
        # Rounded closed housings; each shell has its own matching convex box.
        sides=[i for i in range(3) if i!=axis];a,b=[size[i]/2 for i in sides];points=[]
        for x,y,start in ((a-corner,b-corner,0),(-a+corner,b-corner,90),(-a+corner,-b+corner,180),(a-corner,-b+corner,270)):
            for i in range(9):
                q=(start+i*90/8)*math.pi/180;points.append((x+corner*math.cos(q),y+corner*math.sin(q)))
        rings=[]
        for end in (-.5,.5):
            ring=[]
            for x,y in points:
                p=list(center);p[axis]+=end*size[axis];p[sides[0]]+=x;p[sides[1]]+=y;ring.append(tuple(p))
            rings.append(ring)
        triangles=[];count=len(points)
        for i in range(count):
            j=(i+1)%count;lo,hi=rings
            triangles.extend(((lo[i],lo[j],hi[j]),(lo[i],hi[j],hi[i])))
        for ring,flip in ((rings[0],True),(rings[1],False)):
            p=list(center);p[axis]=ring[0][axis]
            for i in range(count):
                tri=(tuple(p),ring[i],ring[(i+1)%count]);triangles.append(tuple(reversed(tri)) if flip else tri)
        if axis==1:triangles=[tuple(reversed(t)) for t in triangles]
        part(body,name,center,size,triangles,color)
    def bearing(body,name,center,axis,radius,length,color=steel):
        sides=[i for i in range(3) if i!=axis];rings=[];count=64
        for end in (-.5,.5):
            ring=[]
            for i in range(count):
                p=list(center);p[axis]+=end*length;p[sides[0]]+=radius*math.cos(i*2*math.pi/count);p[sides[1]]+=radius*math.sin(i*2*math.pi/count);ring.append(tuple(p))
            rings.append(ring)
        triangles=[]
        for i in range(count):
            j=(i+1)%count;a,b=rings
            triangles.extend([(a[i],a[j],b[j]),(a[i],b[j],b[i])])
        for ring,flip in ((rings[0],True),(rings[1],False)):
            p=list(center);p[axis]=ring[0][axis]
            for i in range(count):
                tri=(tuple(p),ring[i],ring[(i+1)%count]);triangles.append(tuple(reversed(tri)) if flip else tri)
        if axis==1:triangles=[tuple(reversed(t)) for t in triangles]
        size=[radius*2]*3;size[axis]=length
        part(body,name,center,size,triangles,color)
    # Circular rear flange and enclosed fixed motor, with a short ram saddle.
    extrude(mount,'ab_ram_saddle',(0,-.065,.181),(.17,.23,.025),2,.022,steel)
    extrude(mount,'ab_fixed_mount',(0,-.145,.115),(.12,.085,.13),1,.024,shell)
    bearing(mount,'ab_B_mount_flange',(0,-.205,0),1,.095,.018)
    bearing(mount,'ab_B_stator',(0,-.149,0),1,.077,.11,shell)
    bearing(mount,'ab_B_rear_cap',(0,-.212,0),1,.066,.018,dark)
    for i in range(6):
        angle=i*math.pi/3
        bearing(mount,'ab_flange_bolt_'+str(i),(.082*math.cos(angle),-.218,.082*math.sin(angle)),1,.0045,.006,dark)
    # B carries the rounded shoulder and the A motor. The tool sits in front
    # of the ram, so its housing has real clearance throughout the working arc.
    bearing(link,'ab_B_shaft',(0,-.082,0),1,.036,.065)
    bearing(link,'ab_B_rotor',(0,-.045,0),1,.071,.042,shell)
    extrude(link,'ab_shoulder',(0.067,-.005,-.004),(.20,.075,.14),1,.034,shell)
    extrude(link,'ab_side_arm',(.145,.055,-.0325),(.072,.19,.155),0,.035,shell)
    tool=robot.find("link[@name='A 激光切割头']")
    a_joint=robot.find("joint[@name='A']")
    a_joint.find('limit').set('lower',str(math.radians(-46)))
    a_joint.find('limit').set('upper',str(math.radians(50)))
    joint.find('limit').set('lower',str(math.radians(-50)))
    joint.find('limit').set('upper',str(math.radians(50)))
    p=numbers(origin(a_joint).get('xyz'));p[1]=.14;origin(a_joint).set('xyz',vector(p))
    a_origin=numbers(robot.find("joint[@name='A']/origin").get('xyz'))
    bearing(link,'ab_A_bearing',(.135,a_origin[1],a_origin[2]),0,.066,.065,shell)
    bearing(link,'ab_A_motor',(.194,a_origin[1],a_origin[2]),0,.046,.075,dark)
    bearing(link,'ab_A_motor_cap',(.235,a_origin[1],a_origin[2]),0,.039,.009)
    bearing(tool,'ab_A_shaft',(.105-a_origin[0],0,0),0,.033,.065)
    extrude(tool,'ab_tool_mount',(.100-a_origin[0],0,.015),(.018,.12,.17),0,.014,steel)
    for y in (-.041,.041):
        for z in (-.045,.075):bearing(tool,'ab_tool_bolt_'+str(y)+'_'+str(z),(.112-a_origin[0],y,z),0,.0045,.008,dark)
    # Connector bosses are part of their owning motor, never independent joints.
    extrude(mount,'ab_B_connector',(0,-.15,.084),(.05,.042,.022),2,.007,dark)
    extrude(link,'ab_A_connector',(.195,.14,-.018),(.032,.05,.018),2,.006,dark)
    return mount.get('name')


def split_layout(robot, output, kind):
    # Both layouts retain the SAME portal, saddle, ram, rails and axis travels
    # as the dual-head gantries. Only their head/workpiece branches differ.
    robot.remove(robot.find("joint[@name='table_mount']"))
    robot.remove(robot.find("link[@name='工作台']"))
    if kind == '4+1':
        parent = '床身与直线导轨'
        path = MACHINES/'rotary-four-plus-one/RC-1000.urdf'
        keep = ['C 轴轴承座','C 三爪回转卡盘','中央装夹承托盘']
    else:
        parent = '床身与耳轴支承'
        path = MACHINES/'cradle-three-plus-two/TC-800.urdf'
        keep = ['A 摇篮','C 回转工作台']
    source = ET.parse(path).getroot()
    fragment = ET.Element('robot')
    for link in source.findall('link'):
        if link.get('name') in keep:
            fragment.append(copy.deepcopy(link))
    for joint in source.findall('joint'):
        if joint.find('child').get('link') not in keep:
            continue
        joint = copy.deepcopy(joint)
        if joint.find('parent').get('link') == parent:
            joint.find('parent').set('link','床身与导轨')
            if kind == '3+2':
                origin(joint).set('xyz','0 0 0')
        fragment.append(joint)
    copy_meshes(fragment,path,output,'support')
    for child in list(fragment):
        robot.append(child)
    if kind == '3+2':
        base = robot.find("link[@name='床身与导轨']")
        for sign in (-1,1):
            add_pedestal(base,output,(sign*.65,0,-.33),(.1,.3,.66),'cradle_stand_'+str(sign))
        add_pedestal(base,output,(0,0,-.685),(1.5,2.1,.04),'cradle_stand_bridge')


def cover_head_surfaces(robot,output,root_name,tool_name,tolerance=1e-9):
    """Cover every head triangle, including fastener heads, with local boxes.

    Assign each exterior triangle to its closest authored box. Expanding that
    single convex box to include all three vertices covers the complete face,
    while keeping the hollow fork open. This is offline geometry preparation;
    runtime collision still consumes the explicitly emitted independent boxes.
    """
    parents={j.find('child').get('link'):j for j in robot.findall('joint')}
    chain=[];name=tool_name
    while True:
        chain.append(robot.find(f"link[@name='{name}']"))
        if name==root_name:break
        name=parents[name].find('parent').get('link')
    def matrix(node):
        if node is None:return ([0,0,0],[[1,0,0],[0,1,0],[0,0,1]])
        a,b,c=numbers(node.get('rpy','0 0 0'));sx,cx,sy,cy,sz,cz=math.sin(a),math.cos(a),math.sin(b),math.cos(b),math.sin(c),math.cos(c)
        return numbers(node.get('xyz','0 0 0')),[[cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx],[sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx],[-sy,cy*sx,cy*cx]]
    def local(p,pose):
        t,r=pose;return [sum(r[k][i]*(p[k]-t[k]) for k in range(3)) for i in range(3)]
    for link in chain:
        boxes=[]
        for attachment in link.findall('collision'):
            size=numbers(attachment.find('geometry/box').get('size'));pose=matrix(attachment.find('origin'));half=[s/2 for s in size]
            boxes.append(dict(node=attachment,pose=pose,half=half,lo=[-h for h in half],hi=half[:]))
        for visual in link.findall('visual'):
            mesh=visual.find('geometry/mesh');scale=numbers(mesh.get('scale','1 1 1'));t,r=matrix(visual.find('origin'));data=(output/mesh.get('filename')).read_bytes()
            for row in struct.iter_unpack('<12fH',data[84:]):
                triangle=[[sum(r[i][k]*row[offset+k]*scale[k] for k in range(3))+t[i] for i in range(3)] for offset in (3,6,9)]
                coordinates=[[local(p,b['pose']) for p in triangle] for b in boxes]
                if any(all(abs(p[k])<=box['half'][k]+tolerance for p in coords for k in range(3)) for box,coords in zip(boxes,coordinates)):continue
                index=min(range(len(boxes)),key=lambda n:sum(max(0,abs(sum(p[k] for p in coordinates[n])/3)-boxes[n]['half'][k])**2 for k in range(3)))
                box=boxes[index]
                for p in coordinates[index]:
                    for k in range(3):box['lo'][k]=min(box['lo'][k],p[k]-1e-6);box['hi'][k]=max(box['hi'][k],p[k]+1e-6)
        for box in boxes:
            t,r=box['pose'];center=[(box['hi'][k]+box['lo'][k])/2 for k in range(3)]
            origin(box['node']).set('xyz',vector([t[i]+sum(r[i][k]*center[k] for k in range(3)) for i in range(3)]))
            box['node'].find('geometry/box').set('size',vector([box['hi'][k]-box['lo'][k] for k in range(3)]))


def build():
    catalog = []
    for index, (key, kind, label, relative) in enumerate(SPECS):
        source = MACHINES/'industrial-ac/LC-3015.urdf'
        robot = ET.parse(source).getroot()
        output = ROOT/key
        output.mkdir(parents=True, exist_ok=True)
        if kind in ('AC', 'AB', 'BC'):
            root_name, tool_name = 'C 回转叉架', 'A 激光切割头'
            if kind == 'BC':
                # C (Z) carries a B (Y) fork: rotate both the fork and nozzle
                # by +90 degrees around Z, and rotate the A pivot to Y.
                for name in (root_name, tool_name):
                    rotate_link_z(robot.find(f"link[@name='{name}']"), math.pi/2)
                j = robot.find("joint[@name='A']")
                j.set('name', 'B')
                j.find('axis').set('xyz', '0 1 0')
                o = origin(j)
                o.set('xyz', vector(rotate_z(numbers(o.get('xyz')), math.pi/2)))
                rename(robot, tool_name, 'B 激光切割头')
                tool_name = 'B 激光切割头'
            elif kind == 'AB':
                # Outer B (Y), inner A (X). A horizontal motor housing replaces
                # the C spindle; the inner fork remains aligned with X.
                j = robot.find("joint[@name='C']")
                j.set('name', 'B')
                j.find('axis').set('xyz', '0 1 0')
                j.find('limit').set('lower', str(-math.pi/2))
                j.find('limit').set('upper', str(math.pi/2))
                rename(robot, root_name, 'B 回转叉架')
                root_name = 'B 回转叉架'
            support_name = '工作台'
        elif kind == '4+1':
            root_name, tool_name = '固定摆头叉架', 'A 激光切割头'
            j = robot.find("joint[@name='C']")
            j.set('name','head_mount'); j.set('type','fixed')
            j.remove(j.find('axis')); j.remove(j.find('limit'))
            rename(robot,'C 回转叉架',root_name)
            support_name = '中央装夹承托盘'
        else:
            robot.remove(robot.find("joint[@name='C']"))
            robot.remove(robot.find("joint[@name='A']"))
            robot.remove(robot.find("link[@name='C 回转叉架']"))
            robot.remove(robot.find("link[@name='A 激光切割头']"))
            bare_path = MACHINES/'cradle-three-plus-two/TC-800.urdf'
            bare = ET.Element('robot')
            bare.append(copy.deepcopy(ET.parse(bare_path).getroot().find("link[@name='固定激光切割头']")))
            copy_meshes(bare,bare_path,output,'bare')
            robot.append(bare[0])
            joint = ET.SubElement(robot,'joint',name='head_mount',type='fixed')
            ET.SubElement(joint,'parent',link='Z 滑枕')
            ET.SubElement(joint,'child',link='固定激光切割头')
            ET.SubElement(joint,'origin',xyz='0.00299999862909317 -0.003 -0.9019999676942825')
            root_name = tool_name = '固定激光切割头'
            support_name = 'C 回转工作台'
        robot.set('name', 'iCAX '+key+' 龙门 · '+label)
        tool = robot.find(f"link[@name='{tool_name}']/icax_tool")
        tool.set('name', key+' 物理喷嘴')
        # Meshes already copied from support/bare fragments are kept intact.
        original = [mesh for mesh in robot.findall('.//mesh') if not mesh.get('filename').split('/')[-1].startswith(('support_','bare_','cradle_stand_'))]
        fragment = ET.Element('robot')
        for mesh in original: fragment.append(mesh)
        copy_meshes(fragment, source, output, key)
        if kind in ('4+1','3+2'):
            split_layout(robot,output,kind)
        if kind=='AB':root_name=ab_yoke(robot,output)
        cover_head_surfaces(robot,output,root_name,tool_name,tolerance=1e-7 if kind=='AB' else 1e-9)
        write(robot, output, key+'.urdf')
        referenced = {Path(mesh.get('filename')).name for mesh in robot.findall('.//mesh')}
        for path in (output/'meshes').glob('*.stl'):
            if path.name not in referenced:
                path.unlink()
        catalog.append(dict(id=f'6bfa3015-1101-4500-9000-{index+1:012d}',
                            name=label, model=key, kind=kind,
                            sourcePath=f'apps/five-axis-cam/resources/machines/gantry-heads/{key}/{key}.urdf',
                            headRoot=root_name, toolLink=tool_name, supportLink=support_name,
                            headAxes={'AC':['C','A'],'AB':['B','A'],'BC':['C','B'],'4+1':['A'],'3+2':[]}[kind],
                            workpieceAxes={'4+1':['C'],'3+2':['A','C']}.get(kind, [])))
    (ROOT/'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Built '+', '.join(item['model'] for item in catalog))


if __name__ == '__main__':
    build()
