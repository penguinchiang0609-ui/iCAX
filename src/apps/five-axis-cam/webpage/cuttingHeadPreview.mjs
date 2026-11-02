import {previewAxes,draftHeadMotion} from './headMotionDraft.mjs';
import {syncHeadChoicePicker} from './headChoicePicker.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {loadRenderResource} from '../../../iCAX-UI/SDK/Viewport/renderResource.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {loadHeadAssembly} from './cuttingHeadAssembly.mjs';
import {machineForward} from './machineKinematics.mjs';
import {headColliderState,renderHeadColliderTools} from './cuttingHeadColliders.mjs';
import {syncHeadColliderPreview,attachHeadColliderPreview,pickHeadCollider,disposeHeadColliderPreview} from './cuttingHeadColliderPreview.mjs';

export function previewJointAxisLabel(head,node){
 const vector=new THREE.Vector3(...node.axis).normalize().toArray(),index=vector.findIndex(v=>Math.abs(Math.abs(v)-1)<1e-8);
 const label=index>=0&&vector.every((v,i)=>i===index||Math.abs(v)<1e-8)?(vector[index]<0?'−':'')+'XYZ'[index]:vector.map(v=>Number(v.toFixed(3))).join('/');
 return (head.kind==='Imported'?'局部 ':'')+label;
}

const previewDegrees=radians=>Number(THREE.MathUtils.radToDeg(radians).toFixed(9));
const angleText=degrees=>String(Number(degrees.toFixed(3)));

export function renderCuttingHeadPreview(view){
 const s=view.fiveHeadPreview;
 const head=view.scene?.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId)?.headModel;
 const axes=previewAxes(view),requested=view.fiveHeadEditor?.axis||0,selected=axes[requested]?.target==='Head'?requested:axes.findIndex(a=>a.target==='Head');
 const controls=axes.map((axis,i)=>{
  if(axis.target!=='Head')return '';
  const value=Number(s?.axisAngles?.[axis.index]||0)*180/Math.PI;
  return `<div class="five-head-preview-joint-row" data-five-key="head-preview-joint-${axis.index}"><button type="button" data-five-command="five.head-instance.axis:${i}" aria-pressed="${selected===i}">${esc(axis.name)} 试转</button><input type="range" min="${axis.lowerDeg}" max="${axis.upperDeg}" step="any" value="${value}" data-five-head-joint="${i}" aria-label="预览 ${attr(axis.name)} 轴" ${s?.phase==='ready'?'':'disabled'}><output data-five-head-angle="${i}">${angleText(value)}°</output></div>`;
 }).join('');
 return `<section class="five-head-preview" data-five-key="head-preview" aria-label="切割头三维预览">
  <div class="five-head-preview-stage" data-five-key="head-preview-stage"><canvas data-five-key="head-preview-canvas" data-five-head-preview width="${s?.canvas.width||300}" height="${s?.canvas.height||300}" tabindex="0" aria-label="切割头模型，右键拖动旋转，中键拖动平移，滚轮缩放" data-preview-ready="${s?.phase==='ready'}"></canvas>${renderHeadColliderTools(view)}<div class="five-head-preview-status" data-five-key="head-preview-status" role="status" ${s?.phase==='ready'?'hidden':''}>${esc(s?.message||'正在加载模型…')}</div></div>
  <div class="five-head-preview-tools"><span>右键旋转 · 中键平移 · 滚轮缩放</span><button type="button" data-five-head-preview-reset>${s?.phase==='error'?'重试预览':'复位视角'}</button></div>
  ${controls?`<div class="five-head-preview-joints" data-five-key="head-preview-joints"><header><span>预览转角</span><button type="button" data-five-head-joints-reset ${s?.phase==='ready'?'':'disabled'}>关节复位</button></header>${controls}</div>`:''}
  <small class="five-head-preview-legend" data-five-key="head-preview-legend">${esc(s?.legend||'红点：喷嘴端点 · 箭头：光束方向')}</small>
 </section>`;
}

// Preview angles never write the saved head, machine or machining pose.
export function poseCuttingHeadPreview(group,positions){
 const assembly=group.userData.assembly;
 if(positions.length!==assembly.nodes.length||positions.some((v,i)=>!Number.isFinite(v)||assembly.nodes[i].type!=='fixed'&&(v<assembly.nodes[i].lower-1e-9||v>assembly.nodes[i].upper+1e-9)))throw Error('预览关节角度超出范围。');
 const world=machineForward(assembly,positions);
 group.children.forEach((mesh,i)=>{mesh.matrix.copy(world[i]);mesh.matrixWorldNeedsUpdate=true;});
 return {world,tcp:new THREE.Vector3(...assembly.tcp).applyMatrix4(world[assembly.tool]),beam:new THREE.Vector3(...assembly.beam).transformDirection(world[assembly.tool])};
}

function updateMotionPreview(s,force=false){
 if(!s.model)return;
 try{
  const motion=draftHeadMotion(s.head,s.view.fiveHeadEditor?.draft);if(!motion)return;
  const key=JSON.stringify([motion,s.view.fiveHeadEditor?.axis]);if(!force&&s.motionKey===key)return;s.motionKey=key;s.motion=motion;s.axisAngles??={};
  const positions=motion.nodes.map(n=>n.parameterIndex==null?Number(n.position||0):THREE.MathUtils.clamp(s.axisAngles[previewAxes(s.view)[n.parameterIndex]?.index]||0,n.lower,n.upper));
  const world=machineForward(motion,positions),axes=previewAxes(s.view),selected=s.view.fiveHeadEditor?.axis||0;s.motionWorld=world;
  const parameterFor=i=>{for(let n=i;n>=0;n=motion.nodes[n].parent)if(motion.nodes[n].parameterIndex!=null)return motion.nodes[n].parameterIndex;return axes.findIndex(a=>a.target==='Head');};
  if(s.model.userData.assembly)s.model.children.forEach((part,i)=>{part.matrix.copy(world[motion.headNodes[i]]);part.matrixWorldNeedsUpdate=true;const parameter=parameterFor(motion.headNodes[i]);part.traverse(o=>o.userData.previewParameter=parameter);});
  else {s.model.matrixAutoUpdate=false;s.model.matrix.copy(world[motion.headNodes[0]]);s.model.matrixWorldNeedsUpdate=true;s.model.traverse(o=>o.userData.previewParameter=parameterFor(motion.headNodes[0]));}
  if(s.workpiece){s.workpiece.matrix.copy(world[motion.support]);s.workpiece.matrixWorldNeedsUpdate=true;s.workpiece.userData.previewParameter=parameterFor(motion.support);}
  s.model.updateMatrixWorld(true);s.workpiece?.updateMatrixWorld(true);
  s.previewPositions=positions;s.previewCalibrated=s.head.calibrated;showPoseMarkers(s,{world,tcp:new THREE.Vector3(...motion.tcp).applyMatrix4(world[motion.tool]),beam:new THREE.Vector3(...motion.beam).transformDirection(world[motion.tool]),calibrated:s.head.calibrated});render(s);
 }catch{ /* Keep the last valid pose while a numeric field is being typed. */ }
}

function disposeObject(root){
 if(!root)return;
 root.removeFromParent();
 root.traverse(o=>{o.geometry?.dispose();for(const material of Array.isArray(o.material)?o.material:[o.material])material?.dispose();});
}

export function createSchematicCuttingHead(){
 const group=new THREE.Group();group.name='schematic-cutting-head';
 for(const [geometry,z,color] of [[new THREE.ConeGeometry(7,16,24),8,0xa88955],[new THREE.CylinderGeometry(11,7,16,24),24,0xc5cbd3],[new THREE.CylinderGeometry(12,12,24,24),44,0x6b7787]]){
  const mesh=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color,roughness:.55,metalness:.3}));
  mesh.rotation.x=geometry.type==='ConeGeometry'?-Math.PI/2:Math.PI/2;mesh.position.z=z;mesh.raycast=()=>{};group.add(mesh);
 }
 return group;
}

// Display only: immutable part snapshots and their saved joint transforms are
// shared with simulation; the preview never writes calibration or CAD state.
export async function loadCuttingHeadPreview(resources,head,current=()=>true){
 if(head?.kind==='Builtin'&&!head.builtinDefinitionId&&!head.assembly&&!head.previewRef){
  return {group:createSchematicCuttingHead(),tcp:new THREE.Vector3(),beam:new THREE.Vector3(0,0,-1),calibrated:true,schematic:true};
 }
 if(head?.assembly){
  const group=await loadHeadAssembly(resources,head.assembly,current);if(!group)return null;
  const positions=head.assembly.nodes.map(n=>Number(n.position||0));
  return {group,...poseCuttingHeadPreview(group,positions),positions,calibrated:head.calibrated!==false};
 }
 const bound=head?.collisionSource==='MachineInstance',ref=bound?head.machinePreviewRef:head?.previewRef;
 if(!ref?.url)throw Error('当前切割头没有外形模型，请选择或导入模型。');
 const resource=await loadRenderResource(resources,ref);if(!current())return null;
 const data=resource?.data;if(data?.kind!=='mesh'||!data.positions?.length)throw Error('切割头显示模型无效。');
 const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(data.positions,3));
 if(data.indices?.length)geometry.setIndex(Array.from(data.indices));
 if(data.normals?.length===data.positions.length)geometry.setAttribute('normal',new THREE.Float32BufferAttribute(data.normals,3));else geometry.computeVertexNormals();
 const group=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color:0x7c8ba2,roughness:.5,metalness:.25,side:THREE.DoubleSide}));
 group.name=bound?'machine-cutting-head':'imported-cutting-head';
 return {group,tcp:new THREE.Vector3(...(head.tcpLocalPosition||[0,0,0])),beam:new THREE.Vector3(...(head.beamLocalDirection||[0,0,-1])).normalize(),calibrated:!!head.calibrated};
}

function paintStatus(s){
 const status=s.host.querySelector('.five-head-preview-status');
 if(status){status.hidden=s.phase==='ready';status.textContent=s.message;}
 const reset=s.host.querySelector('[data-five-head-preview-reset]');if(reset)reset.textContent=s.phase==='error'?'重试预览':'复位视角';
 const legend=s.host.querySelector('.five-head-preview-legend');if(legend)legend.textContent=s.legend||'红点：喷嘴端点 · 箭头：光束方向';
 for(const input of s.host.querySelectorAll('[data-five-head-joint]')){
  input.disabled=s.phase!=='ready';const value=previewDegrees(s.axisAngles?.[previewAxes(s.view)[Number(input.dataset.fiveHeadJoint)]?.index]||0);input.value=String(value);
  const output=s.host.querySelector(`[data-five-head-angle="${input.dataset.fiveHeadJoint}"]`);if(output)output.textContent=angleText(value)+'°';
 }
 const jointReset=s.host.querySelector('[data-five-head-joints-reset]');if(jointReset)jointReset.disabled=s.phase!=='ready';
 s.canvas.dataset.previewReady=String(s.phase==='ready');
}
function render(s){if(!s.dead){syncHeadColliderPreview(s);s.renderer.render(s.scene,s.camera);paintStatus(s);}}
function showPoseMarkers(s,result){
 disposeObject(s.markers);s.markers=new THREE.Group();
 if(result.calibrated){
  const dot=new THREE.Mesh(new THREE.SphereGeometry(s.radius*.018,12,8),new THREE.MeshBasicMaterial({color:0xe65142,depthTest:false}));dot.position.copy(result.tcp);dot.renderOrder=10;
  s.markers.add(dot,new THREE.ArrowHelper(result.beam,result.tcp,s.radius*.3,0xe65142,s.radius*.07,s.radius*.035));
 }
 const assembly=s.motion||s.model?.userData.assembly;
 if(assembly)for(let i=0;i<assembly.nodes.length;i++){
  const node=assembly.nodes[i];if(node.type==='fixed'||s.motion&&node.parameterIndex==null)continue;
  const axis=new THREE.ArrowHelper(new THREE.Vector3(...node.axis).transformDirection(result.world[i]),new THREE.Vector3().setFromMatrixPosition(result.world[i]),s.radius*.65,{A:0xd94343,B:0x269955,C:0x3276da}[node.joint]||0x777777,s.radius*.09,s.radius*.045);
  axis.userData.previewJointAxis=node.joint;axis.traverse(o=>o.userData.previewParameter=node.parameterIndex);s.markers.add(axis);
 }
 s.scene.add(s.markers);
}
function fit(s){
 if(!s.model)return;
 s.camera.position.copy(s.target).add(new THREE.Vector3(1.4,s.model.userData.assembly?.kind==='AB'?1.8:-1.8,1).normalize().multiplyScalar(s.radius/Math.sin(THREE.MathUtils.degToRad(s.camera.fov/2))*1.15/Math.min(s.camera.aspect,1)));
 s.camera.lookAt(s.target);render(s);
}
function resize(s){
 const rect=s.canvas.getBoundingClientRect();if(rect.width<=0||rect.height<=0||s.dead)return;
 s.renderer.setPixelRatio(Math.min(s.host.ownerDocument.defaultView.devicePixelRatio||1,1.5));
 s.renderer.setSize(Math.round(rect.width),Math.round(rect.height),false);
 s.camera.aspect=rect.width/rect.height;s.camera.updateProjectionMatrix();render(s);
}
function createPreview(context,view,host,canvas){
 const window=host.ownerDocument.defaultView;
 const s={host,canvas,context,view,phase:'loading',message:'正在加载模型…',token:0,dead:false,target:new THREE.Vector3(),radius:1};
 s.renderer=new THREE.WebGLRenderer({canvas,antialias:true,alpha:false});s.renderer.setClearColor(0xedf3f5,1);
 s.scene=new THREE.Scene();s.camera=new THREE.PerspectiveCamera(34,1,.001,10000);s.camera.up.set(0,0,1);
 s.scene.add(new THREE.HemisphereLight(0xffffff,0x627c8d,2.5));
 const light=new THREE.DirectionalLight(0xffffff,3);light.position.set(1,-2,3);s.scene.add(light);
 let drag;
 const down=e=>{if(e.button!==1&&e.button!==2)return;e.preventDefault();e.stopPropagation();canvas.focus({preventScroll:true});drag={id:e.pointerId,x:e.clientX,y:e.clientY,pan:e.button===1};canvas.setPointerCapture(e.pointerId);};
 const move=e=>{
  if(!drag||drag.id!==e.pointerId)return;e.preventDefault();e.stopPropagation();
  const dx=e.clientX-drag.x,dy=e.clientY-drag.y;drag.x=e.clientX;drag.y=e.clientY;
  const offset=s.camera.position.clone().sub(s.target),right=new THREE.Vector3(1,0,0).applyQuaternion(s.camera.quaternion);
  if(drag.pan){
   const scale=offset.length()*2*Math.tan(THREE.MathUtils.degToRad(s.camera.fov/2))/canvas.clientHeight;
   const pan=right.multiplyScalar(-dx*scale).add(new THREE.Vector3(0,1,0).applyQuaternion(s.camera.quaternion).multiplyScalar(dy*scale));s.target.add(pan);s.camera.position.add(pan);
  }else{
   offset.applyAxisAngle(new THREE.Vector3(0,0,1),-dx*.008);
   const tilted=offset.clone().applyAxisAngle(right,-dy*.008);if(Math.abs(tilted.clone().normalize().z)<.995)offset.copy(tilted);
   s.camera.position.copy(s.target).add(offset);s.camera.lookAt(s.target);
  }render(s);
 };
 const up=e=>{if(drag?.id===e.pointerId){e.stopPropagation();drag=null;if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId);}};
 const wheel=e=>{e.preventDefault();e.stopPropagation();const offset=s.camera.position.clone().sub(s.target);offset.multiplyScalar(Math.exp(THREE.MathUtils.clamp(e.deltaY*.001,-.35,.35))).clampLength(s.radius*.15,s.radius*80);s.camera.position.copy(s.target).add(offset);render(s);};
 const menu=e=>{e.preventDefault();e.stopPropagation();};
 const reset=()=>{if(s.phase==='error'){s.key=null;syncCuttingHeadPreview(s.context,s.view);}else fit(s);};
 const click=e=>{
  if(e.target.closest('[data-five-head-preview-reset]')){e.stopPropagation();reset();}
  else if(e.target.closest('[data-five-head-joints-reset]')){e.stopPropagation();s.axisAngles={};updateMotionPreview(s,true);}
  else if(e.target===canvas&&e.button===0&&s.model){
   if(pickHeadCollider(s,e)){e.stopPropagation();return;}
   const rect=canvas.getBoundingClientRect(),ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2((e.clientX-rect.left)/rect.width*2-1,-(e.clientY-rect.top)/rect.height*2+1),s.camera);
   const hits=ray.intersectObjects([s.markers,s.model,s.workpiece].filter(Boolean),true);const hit=hits.find(h=>Number.isInteger(h.object.userData.previewParameter));
   if(hit)s.host.ownerDocument.querySelector('[data-five-command="five.head-instance.axis:'+hit.object.userData.previewParameter+'"]')?.click();
  }
 };
 const input=e=>{
  if(!e.target.hasAttribute('data-five-head-joint')||s.phase!=='ready')return;
  e.stopPropagation();const a=previewAxes(view)[Number(e.target.dataset.fiveHeadJoint)];if(!a)return;s.axisAngles??={};s.axisAngles[a.index]=THREE.MathUtils.degToRad(Number(e.target.value));updateMotionPreview(s,true);
 };
 const key=e=>{if(e.target===canvas&&['f','r'].includes(e.key.toLowerCase())){e.preventDefault();e.stopPropagation();fit(s);}};
 const events={pointerdown:down,pointermove:move,pointerup:up,pointercancel:up,lostpointercapture:up,wheel,contextmenu:menu,keydown:key};
 for(const [name,fn] of Object.entries(events))canvas.addEventListener(name,fn,{passive:false});host.addEventListener('click',click);host.addEventListener('input',input);
 s.observer=new window.ResizeObserver(()=>resize(s));s.observer.observe(canvas);resize(s);
 s.renderPreview=()=>render(s);attachHeadColliderPreview(s);
 s.dispose=()=>{s.dead=true;++s.token;disposeHeadColliderPreview(s);s.observer.disconnect();for(const [name,fn] of Object.entries(events))canvas.removeEventListener(name,fn);host.removeEventListener('click',click);host.removeEventListener('input',input);disposeObject(s.model);disposeObject(s.workpiece);disposeObject(s.markers);s.renderer.dispose();s.renderer.forceContextLoss();};
 return s;
}

export function disposeCuttingHeadPreview(view){view.fiveHeadPreview?.dispose();view.fiveHeadPreview=null;}
export function syncCuttingHeadPreview(context,view,ops){
 if(ops)syncHeadChoicePicker(context,view,ops);
 if(ops)headColliderState(view).binding={context,view,ops};
 const host=context.mount?.querySelector('[data-five-key="head-preview"]'),canvas=host?.querySelector('canvas');
 if(view.disposed||context.isCurrentProject?.()===false||view.fiveAxisArea!=='toolpath'||!view.fiveSimulation?.headOpen||!canvas){disposeCuttingHeadPreview(view);return;}
 let s=view.fiveHeadPreview;
 if(s?.canvas!==canvas){disposeCuttingHeadPreview(view);try{s=view.fiveHeadPreview=createPreview(context,view,host,canvas);}catch(error){const status=host.querySelector('.five-head-preview-status');status.hidden=false;status.textContent='无法显示三维预览：'+error.message;return;}}
 s.context=context;resize(s);
 const setup=view.scene?.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId),head=setup?.headModel,sim=view.fiveSimulation;
 const requested=sim.queuedBuiltinHeadId??(sim.request?.payload.kind==='Builtin'?sim.request.payload.builtinDefinitionId:null);
 // Instance labels/profile provenance do not alter this physical display.
 // Effective geometry, joint frames/limits and TCP remain part of the key.
 const physicalHead=[head?.instance?.id,head?.kind,head?.previewRef,head?.machinePreviewRef,head?.assembly?.nodes.map(n=>n.previewRef)];
 const waiting=requested&&requested!==head?.builtinDefinitionId,key=JSON.stringify([setup?.id,physicalHead,waiting?requested:null]);
 s.head=head;if(s.key===key){updateMotionPreview(s);render(s);return;}
 s.key=key;const token=++s.token;disposeHeadColliderPreview(s);attachHeadColliderPreview(s);disposeObject(s.model);disposeObject(s.workpiece);s.workpiece=null;s.motionKey=null;s.axisAngles={};disposeObject(s.markers);s.model=null;s.motion=null;s.motionWorld=null;s.markers=null;s.phase='loading';s.message=waiting?'正在更新所选型号…':'正在加载模型…';s.legend='';render(s);
 if(waiting)return;
 const current=()=>!s.dead&&token===s.token&&view.fiveHeadPreview===s&&!view.disposed&&s.context.isCurrentProject?.()!==false&&canvas.isConnected;
 void loadCuttingHeadPreview(context.sceneProxy.resources,head,current).then(result=>{
  if(!current()){disposeObject(result?.group);return;}if(!result)return;
  s.model=result.group;s.scene.add(s.model);s.model.updateMatrixWorld(true);
  s.model.traverse(o=>{if(o.isMesh)o.raycast=THREE.Mesh.prototype.raycast;});

  updateMotionPreview(s,true);
  const box=new THREE.Box3().setFromObject(s.model);if(s.workpiece)box.expandByObject(s.workpiece);if(box.isEmpty())throw Error('切割头模型为空。');
  box.getCenter(s.target);s.radius=Math.max(box.getSize(new THREE.Vector3()).length()/2,.01);
  if(!s.motion){s.previewPositions=result.positions;s.previewCalibrated=result.calibrated;showPoseMarkers(s,result);}
  s.legend=result.schematic?'内置示意头 · 请选择具体型号查看实际外形':result.calibrated?'红点：喷嘴端点 · 箭头：光束方向':'模型原始坐标 · 尚未标定喷嘴端点与光束方向';
  if(head?.motion)s.legend+='';
  s.camera.near=s.radius*.001;s.camera.far=s.radius*200;s.camera.updateProjectionMatrix();s.phase='ready';s.message='';updateMotionPreview(s,true);fit(s);
 }).catch(error=>{if(current()){disposeObject(s.model);disposeObject(s.markers);s.model=null;s.markers=null;s.phase='error';s.message='预览加载失败：'+error.message;render(s);}});
}
