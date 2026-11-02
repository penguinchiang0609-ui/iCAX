import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machineForward} from './machineKinematics.mjs';
import {loadRenderResource} from '../../../iCAX-UI/SDK/Viewport/renderResource.mjs';

const Z=new THREE.Vector3(0,0,1),TAU=2*Math.PI;
const wrap=v=>Math.atan2(Math.sin(v),Math.cos(v));
const inRange=(v,n)=>v>=Number(n.lower)-1e-8&&v<=Number(n.upper)+1e-8;

export function rigidHeadDirectionSupported(pose){
 return new THREE.Vector3(0,0,1).applyQuaternion(pose.rotation).distanceToSquared(Z)<1e-10;
}

// Solve the declared rotary chain, including its real joint axes, origins and
// limits. XYZ translation is external; no joint is inferred from a mesh/name.
function declaredHeadPose(assembly,direction,previous){
 const nodes=assembly.nodes,chain=new Set();for(let i=assembly.tool;i>=0;i=nodes[i].parent)chain.add(i);
 const rotary=nodes.map((n,i)=>chain.has(i)&&(n.type==='revolute'||n.type==='continuous')?i:-1).filter(i=>i>=0);
 const desired=new THREE.Vector3(...direction).normalize().negate();
 if(!desired.toArray().every(Number.isFinite)||desired.lengthSq()<.9)throw Error('刀轴方向无效。');
 const beamAt=world=>new THREE.Vector3(...assembly.beam).transformDirection(world[assembly.tool]);
 const finish=(values,world,root=new THREE.Matrix4())=>{
  world=world.map(m=>root.clone().multiply(m));const tcp=new THREE.Vector3(...assembly.tcp).applyMatrix4(world[assembly.tool]);
  const shift=new THREE.Matrix4().makeTranslation(...tcp.clone().negate().toArray());world.forEach(m=>m.premultiply(shift));
  return {valid:true,values,world,tcp,angles:Object.fromEntries(rotary.map(i=>[nodes[i].joint,values[i]]))};
 };
 const clamp=(i,v)=>nodes[i].type==='continuous'?v:THREE.MathUtils.clamp(v,Number(nodes[i].lower),Number(nodes[i].upper));
 const initial=nodes.map((n,i)=>n.type==='fixed'?0:clamp(i,Number(previous[n.joint]??n.position)));
 if(!rotary.length){
  if(desired.distanceToSquared(new THREE.Vector3(0,0,-1))>1e-10)return {valid:false,reason:'刚性刀头没有旋转轴，不能执行倾斜刀轴；请使用三轴刀路或选择带关节的刀头。'};
  const world=machineForward(assembly,initial),root=new THREE.Matrix4().makeRotationFromQuaternion(new THREE.Quaternion().setFromUnitVectors(beamAt(world),desired));
  return finish(initial,world,root);
 }
 const solve=(matrix,rhs)=>{
  const n=rhs.length,a=matrix.map((row,i)=>[...row,rhs[i]]);
  for(let i=0;i<n;i++){let pivot=i;for(let j=i+1;j<n;j++)if(Math.abs(a[j][i])>Math.abs(a[pivot][i]))pivot=j;
   [a[i],a[pivot]]=[a[pivot],a[i]];if(Math.abs(a[i][i])<1e-15)return null;
   const scale=a[i][i];for(let k=i;k<=n;k++)a[i][k]/=scale;
   for(let j=0;j<n;j++)if(j!==i){const factor=a[j][i];for(let k=i;k<=n;k++)a[j][k]-=factor*a[i][k];}
  }return a.map(row=>row[n]);
 };
 const seeds=[initial,nodes.map((n,i)=>n.type==='fixed'?0:clamp(i,Number(n.position)))];
 for(const i of rotary)for(const delta of [Math.PI/2,-Math.PI/2,Math.PI]){const seed=[...initial];seed[i]=clamp(i,seed[i]+delta);seeds.push(seed);}
 for(const seed of seeds){const values=[...seed];
  for(let iteration=0;iteration<60;iteration++){
   const world=machineForward(assembly,values),beam=beamAt(world),error=desired.clone().sub(beam);
   if(error.lengthSq()<1e-14)return finish(values,world);
   const derivatives=rotary.map(i=>{
    const n=nodes[i],frame=new THREE.Matrix4().set(...n.origin);if(n.parent>=0)frame.premultiply(world[n.parent]);
    return new THREE.Vector3(...n.axis).transformDirection(frame).cross(beam);
   });
   const matrix=derivatives.map((a,i)=>derivatives.map((b,j)=>a.dot(b)+(i===j?1e-5:0))),step=solve(matrix,derivatives.map(j=>j.dot(error)));
   if(!step)break;let improved=false;
   for(let factor=1;factor>=1/32;factor/=2){const next=[...values];rotary.forEach((i,k)=>next[i]=clamp(i,values[i]+THREE.MathUtils.clamp(step[k],-.45,.45)*factor));
    if(beamAt(machineForward(assembly,next)).distanceToSquared(desired)<error.lengthSq()-1e-16){values.splice(0,values.length,...next);improved=true;break;}
   }if(!improved)break;
  }
 }
 return {valid:false,reason:'当前刀轴方向超出已声明旋转关节的可达范围。'};
}

// This is a head-structure preview in setup coordinates. The installed full
// machine owns travel limits, support motion, IK and continuous certification.
// 3+2 and 4+1 include the inverse workpiece rotation in this relative view.
export function headAssemblyPose(assembly,direction,previous={}){
 if(assembly.source!=='MachineInstance')return declaredHeadPose(assembly,direction,previous);
 const d=new THREE.Vector3(...direction).normalize(),nodes=assembly.nodes,kind=assembly.kind;
 if(!d.toArray().every(Number.isFinite)||d.lengthSq()<.9)throw Error('刀轴方向无效。');
 let a=0,b=0,c=0,root=new THREE.Matrix4(),angles={};
 if(kind==='3+2')root.makeRotationFromQuaternion(new THREE.Quaternion().setFromUnitVectors(Z,d));
 else {
  if(kind==='AB'){a=-Math.asin(THREE.MathUtils.clamp(d.y,-1,1));b=Math.atan2(d.x,d.z);}
  else {const tilt=Math.atan2(Math.hypot(d.x,d.y),d.z);a=kind==='BC'?0:tilt;b=kind==='BC'?tilt:0;c=Math.hypot(d.x,d.y)<1e-9?previous.C||0:kind==='BC'?Math.atan2(d.y,d.x):Math.atan2(d.x,-d.y);}
  const candidates=kind==='AB'?[{A:a,B:b},{A:wrap(Math.PI-a),B:wrap(b+Math.PI)}]:kind==='BC'?[{B:b,C:c},{B:-b,C:wrap(c+Math.PI)}]:[{A:a,C:c},{A:-a,C:wrap(c+Math.PI)}];
  let best=null,cost=Infinity;
  for(const candidate of candidates){let valid=true,score=0;const resolved={...candidate};
   for(const n of nodes){if(n.type==='fixed')continue;let v=candidate[n.joint];if(!Number.isFinite(v)){valid=false;break;}
    const choices=[v-TAU,v,v+TAU].filter(value=>inRange(value,n));if(!choices.length){valid=false;break;}
    const reference=previous[n.joint]??Number(n.position??0);choices.sort((x,y)=>Math.abs(x-reference)-Math.abs(y-reference));v=choices[0];resolved[n.joint]=v;score+=(v-reference)**2;
   }
   if(valid&&score<cost){best=resolved;cost=score;}
  }
  if(!best)return {valid:false,reason:'当前刀轴超出此刀头的回转范围，请在机床仿真中核对装夹与可达性。'};
  angles=best;if(kind==='4+1')root.makeRotationZ(angles.C);
 }
 const values=nodes.map(n=>n.type==='fixed'?0:angles[n.joint]??0),world=machineForward({nodes},values).map(m=>root.clone().multiply(m));
 const tcp=new THREE.Vector3(...assembly.tcp).applyMatrix4(world[assembly.tool]);
 const beam=new THREE.Vector3(...assembly.beam).transformDirection(world[assembly.tool]);
 if(beam.dot(d.clone().negate())<1-1e-7)return {valid:false,reason:'刀头结构与刀轴方向不一致，请在机床仿真中检查。'};
 for(const matrix of world)matrix.premultiply(new THREE.Matrix4().makeTranslation(...tcp.clone().negate().toArray()));
 return {valid:true,angles,values,world,tcp};
}

export async function loadHeadAssembly(resources,assembly,current=()=>true){
 const result=new THREE.Group();result.name='articulated-cutting-head';
 // Fetch immutable embedded part snapshots together. No external source path
 // is needed when reopening a saved project on a different computer.
 const data=await Promise.all(assembly.nodes.map(async node=>node.previewRef?.url?(await loadRenderResource(resources,node.previewRef))?.data:null));
 if(!current())return null;
 for(let i=0;i<data.length;i++){
  const part=data[i];if(!part){const frame=new THREE.Group();frame.name=assembly.nodes[i].name;frame.matrixAutoUpdate=false;result.add(frame);continue;}
  if(part.kind!=='mesh')throw Error('刀头装配显示模型无效。');
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(part.positions,3));
  if(part.indices?.length)geometry.setIndex(Array.from(part.indices));
  if(part.normals?.length===part.positions.length)geometry.setAttribute('normal',new THREE.Float32BufferAttribute(part.normals,3));else geometry.computeVertexNormals();
  const mesh=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color:assembly.nodes[i].type==='fixed'?0x9aa5b4:i===assembly.tool?0x60798a:0x217c83,roughness:.5,metalness:.35,side:THREE.DoubleSide}));
  mesh.name=assembly.nodes[i].name;mesh.matrixAutoUpdate=false;mesh.raycast=()=>{};result.add(mesh);
 }
 result.userData.assembly=assembly;result.userData.angles={};return result;
}

export function checkedHeadAssemblyPose(report,cursor){
 const coverage=report?.headCoverage;if(coverage?.scope!=='CompleteSelectedHeadAssembly')return null;
 const index=Math.min(Math.floor(cursor),Math.max(0,(report.frames?.length??2)-2)),segment=report.segments?.find(s=>Number(s.index)===index),head=segment?.headPose;
 const keyposes=coverage.keyposes;
 if(Array.isArray(keyposes)&&keyposes.length){
  const keys=keyposes.map(k=>({...k,frameIndex:Number(k.frameIndex)})).sort((a,b)=>a.frameIndex-b.frameIndex);
  const exact=keys.find(k=>k.frameIndex===cursor);
  if(exact)return exact.joints?.length===coverage.nodes?.length?interpolatedHeadPose(coverage,report.frames,index,cursor-index,[...exact.joints]):null;
  const end=keys.findIndex(k=>k.frameIndex>=cursor),b=keys[end<0?keys.length-1:end],a=keys[Math.max(0,end<0?keys.length-1:end-1)];
  if(a.frameIndex<=cursor&&cursor<=b.frameIndex&&a.joints?.length===coverage.nodes?.length&&b.joints?.length===coverage.nodes?.length){
   let length=0,travel=0;for(let i=a.frameIndex;i<b.frameIndex;i++){const first=report.frames?.[i]?.position,last=report.frames?.[i+1]?.position;if(!first||!last)return null;const distance=new THREE.Vector3(...first).distanceTo(new THREE.Vector3(...last));length+=distance;if(i<index)travel+=distance;else if(i===index)travel+=distance*(cursor-index);}
   const t=b.frameIndex===a.frameIndex?0:length>1e-12?travel/length:(cursor-a.frameIndex)/(b.frameIndex-a.frameIndex);
   return interpolatedHeadPose(coverage,report.frames,index,cursor-index,a.joints.map((q,i)=>q+(b.joints[i]-q)*t));
  }
  return null;
 }
 if(!head||head.frame!=='Setup')return null;
 const fraction=cursor-index;
 if(Math.abs(fraction-Number(segment.fraction))<1e-8)return head;
 const knots=head.motion,nodes=coverage.nodes;
 if(!Array.isArray(knots)||knots.length<2||!Array.isArray(nodes)||!Array.isArray(coverage.headNodes))return null;
 const after=knots.findIndex(k=>Number(k.fraction)>=fraction),b=knots[Math.max(1,after<0?knots.length-1:after)],a=knots[Math.max(0,after<0?knots.length-2:after-1)];
 if(!Array.isArray(a.joints)||!Array.isArray(b.joints)||a.joints.length!==nodes.length||b.joints.length!==nodes.length||![...a.joints,...b.joints].every(Number.isFinite))return null;
 const t=THREE.MathUtils.clamp((fraction-Number(a.fraction))/(Number(b.fraction)-Number(a.fraction)),0,1);
 const joints=a.joints.map((q,i)=>q+(b.joints[i]-q)*t);
 return interpolatedHeadPose(coverage,report.frames,index,fraction,joints);
}
function interpolatedHeadPose(coverage,frames,index,fraction,joints){
 const nodes=coverage.nodes;
 if(coverage.motion==='KeyposeJointInterpolationWithOriginalTCP'){
  const first=frames?.[index]?.position,last=frames?.[index+1]?.position??first;
  if(!first?.every(Number.isFinite)||!last?.every(Number.isFinite)||!coverage.tcp?.every(Number.isFinite))return null;
  for(let k=1;k<=3;k++)joints[k]=0;
  const raw=machineForward({nodes},joints),target=new THREE.Vector3(...first).lerp(new THREE.Vector3(...last),fraction).applyMatrix4(raw[Number(coverage.support)]),tcp=new THREE.Vector3(...coverage.tcp).applyMatrix4(raw[Number(coverage.tool)]),shift=target.sub(tcp);
  [joints[1],joints[2],joints[3]]=shift.toArray();
 }
 const world=machineForward({nodes},joints),support=world[Number(coverage.support)]?.clone().invert();if(!support)return null;
 const transforms=coverage.headNodes.map(i=>world[Number(i)]?.clone().premultiply(support));if(transforms.some(m=>!m))return null;
 return {frame:'Setup',partTransforms:transforms.map(m=>m.clone().transpose().toArray())};
}

export function paintHeadAssembly(group,pose,checked=null){
 if(checked){
  const transforms=checked.partTransforms;
  const valid=checked.frame==='Setup'&&Array.isArray(transforms)&&transforms.length===group.children.length&&transforms.every(m=>Array.isArray(m)&&m.length===16&&m.every(Number.isFinite)&&Math.abs(m[12])+Math.abs(m[13])+Math.abs(m[14])+Math.abs(m[15]-1)<1e-8);
  group.visible=valid;if(!valid)return {valid:false,reason:'检测记录中的切割头姿态无效，请重新检测本条。'};
  const inverse=new THREE.Matrix4().compose(pose.position,pose.rotation,new THREE.Vector3(1,1,1)).invert();
  group.children.forEach((mesh,i)=>{mesh.matrix.copy(inverse).multiply(new THREE.Matrix4().set(...transforms[i]));mesh.matrixWorldNeedsUpdate=true;});
  group.userData.poseSource='NativeCollision';return {valid:true,source:'NativeCollision'};
 }
 const direction=Z.clone().applyQuaternion(pose.rotation).toArray(),result=headAssemblyPose(group.userData.assembly,direction,group.userData.angles);
 group.visible=result.valid;if(!result.valid)return result;
 group.userData.angles=result.angles;
 group.userData.poseSource='HeadPreview';
 const inverse=new THREE.Matrix4().makeRotationFromQuaternion(pose.rotation.clone().invert());
 group.children.forEach((mesh,i)=>{mesh.matrix.copy(inverse).multiply(result.world[i]);mesh.matrixWorldNeedsUpdate=true;});
 return result;
}
