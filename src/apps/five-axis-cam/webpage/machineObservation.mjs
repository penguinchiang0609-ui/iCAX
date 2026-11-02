import {machineSegmentJoints} from './machineTrajectory.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {machinePlaybackActive,camMachinePlayback} from './machinePlaybackContext.mjs';
import {machineForward,machineMatrix} from './machineKinematics.mjs';
import {findTableOccluders} from './machineTableOcclusion.mjs';
import {prepareMeshPicking} from '../../../iCAX-UI/SDK/Viewport/meshPicking.mjs';

const state=view=>view.fiveMachineObservation??={automatic:true,showTrail:true,showRapid:true,showCut:true,faded:new Map(),lastCheck:0};
const observationActive=view=>camMachinePlayback(view)||view.fiveAxisArea==='simulation'&&['machine-instances','machine-simulation'].includes(activeTool(view));
const tableKey=id=>`icax.machine-observation.table:${id}`;
const selectedMachine=view=>view.scene?.machines?.find(m=>m.entityId===view.fiveMachine?.machineId);
// An explicit viewing preference never assigns a physical work-support role.
export function setMachineObservationTable(view,entityId){
 const machine=selectedMachine(view),id=String(entityId||'');
 if(!machine||id&&!machine.elements?.some(e=>e.entityId===id&&e.kind!=='machine'))throw Error('请选择当前机床的工作台部件。');
 const s=state(view);s.tableMachineId=machine.entityId;s.tableId=id;s.signature='';
 try{if(id)globalThis.localStorage?.setItem(tableKey(machine.entityId),id);else globalThis.localStorage?.removeItem(tableKey(machine.entityId));}catch{}
}
const rapid=(frame,index)=>frame?.laser===false||['Rapid','Approach'].includes(frame?.role);
function restore(s,object){
 const entry=s.faded.get(object);if(!entry)return;
 if(object.material===entry.faded){
  // Selection may change while an occluder uses its temporary transparent material.
  // Carry the latest selection colour back without carrying the faded opacity.
  const originals=Array.isArray(entry.original)?entry.original:[entry.original],faded=Array.isArray(entry.faded)?entry.faded:[entry.faded];
  originals.forEach((m,i)=>{const f=faded[i];if(!f)return;m.color?.copy(f.color);m.emissive?.copy(f.emissive);if('emissiveIntensity' in m)m.emissiveIntensity=f.emissiveIntensity;if('linewidth' in m)m.linewidth=f.linewidth;
   // Selection may have captured this temporary material. Its later cleanup
   // must restore the real material's opacity, rather than the occlusion fade.
   const selected=object.userData.selectionMaterialState?.[i];if(selected)for(const key of ['opacity','transparent','depthWrite'])selected[key]=m[key];
  });
  object.material=entry.original;
 }
 for(const material of Array.isArray(entry.faded)?entry.faded:[entry.faded])material.dispose();s.faded.delete(object);
}
function fade(s,object){
 const entry=s.faded.get(object);if(entry&&object.material===entry.faded){
  // Highlighting may restore an interaction snapshot on this same material.
  // Keep the body translucent without replacing its colour or its material.
  const original=Array.isArray(entry.original)?entry.original:[entry.original],materials=Array.isArray(entry.faded)?entry.faded:[entry.faded];
  materials.forEach((m,i)=>{m.opacity=Math.min(original[i].opacity,.16);m.transparent=true;m.depthWrite=false;const saved=object.userData.selectionMaterialState?.[i];if(saved)for(const key of ['opacity','transparent','depthWrite'])saved[key]=m[key];});return;
 }
 if(entry)restore(s,object);
 const original=object.material;
 const clone=m=>{const c=m.clone();c.transparent=true;c.opacity=Math.min(m.opacity,.16);c.depthWrite=false;return c;};
 const faded=Array.isArray(original)?original.map(clone):clone(original);s.faded.set(object,{original,faded});object.material=faded;
 (Array.isArray(faded)?faded:[faded]).forEach((m,i)=>{const saved=object.userData.selectionMaterialState?.[i];if(saved)for(const key of ['opacity','transparent','depthWrite'])saved[key]=m[key];});
}
function clearTrail(s){
 if(!s.trail)return;s.trail.root.removeFromParent();s.trail.root.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});s.trail=null;
}
export function disposeMachineObservation(view){
 const s=view?.fiveMachineObservation;if(!s)return;cancelAnimationFrame(s.animation);s.animation=0;
 for(const object of [...s.faded.keys()])restore(s,object);clearTrail(s);s.signature='';
}
export function machineObservationField(view,key,value){
 if(!['observeAutomatic','observeTrail','observeRapid','observeCut'].includes(key))return false;
 const s=state(view),field={observeAutomatic:'automatic',observeTrail:'showTrail',observeRapid:'showRapid',observeCut:'showCut'}[key];
 s[field]=value===true||value==='true';s.signature='';return true;
}
export function renderMachineObservation(view){
 if(!observationActive(view))return '';
 const s=state(view),check=(id,label,on)=>`<label class="five-machine-observe"><input type="checkbox" data-five-machine-field="${id}" ${on?'checked':''}>${label}</label>`;
 return `<div class="five-machine-observation">${camMachinePlayback(view)?'':check('observeAutomatic','自动淡化遮挡',s.automatic)}${machinePlaybackActive(view)?check('observeTrail','已走轨迹',s.showTrail)+check('observeRapid','空移 · 红色虚线',s.showRapid)+check('observeCut','切割 · 红色实线',s.showCut):''}</div>`;
}
function localTCP(report,world){
 return new THREE.Vector3(...report.configuration.tcp).applyMatrix4(world[report.tool]).applyMatrix4(world[report.support].clone().multiply(machineMatrix(report.configuration.placement)).invert());
}
function buildTrail(view,report){
 const s=state(view);clearTrail(s);
 const root=new THREE.Group();root.name='MachineExecutedTrail';root.matrixAutoUpdate=false;root.userData.machineExecutedTrail=true;
 const buckets=[{positions:[],ends:[]},{positions:[],ends:[]}];
 for(let i=1;i<report.frames.length;i++){
  const a=report.frames[i-1].joints,b=report.frames[i].joints;
  // Sample the actual joint interpolation, including rotary-table movement.
  // Frame positions alone would incorrectly draw rotary arcs as straight cuts.
  const divisions=Math.min(64,Math.max(1,...a.map((q,k)=>Math.ceil(Math.abs(b[k]-q)/(report.nodes[k].type==='prismatic'?10:Math.PI/180)))));
  let previous=localTCP(report,machineForward(report,a));const bucket=buckets[rapid(report.frames[i-1],i-1)?0:1];
  for(let step=1;step<=divisions;step++){
   const fraction=step/divisions,point=localTCP(report,machineForward(report,machineSegmentJoints(report.frames[i-1],report.frames[i],fraction)));
   bucket.positions.push(...previous.toArray(),...point.toArray());bucket.ends.push(i-1+fraction);previous=point;
  }
 }
 const lines=buckets.map((bucket,index)=>{
  const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(bucket.positions,3));geometry.setDrawRange(0,0);
  const material=index===0?new THREE.LineDashedMaterial({color:0xff3030,dashSize:6,gapSize:4,depthTest:false,depthWrite:false}):new THREE.LineBasicMaterial({color:0xff3030,depthTest:false,depthWrite:false});
  const line=new THREE.LineSegments(geometry,material);line.computeLineDistances();line.renderOrder=40;line.raycast=()=>{};root.add(line);
  const tail=new THREE.LineSegments(new THREE.BufferGeometry().setAttribute('position',new THREE.Float32BufferAttribute(new Float32Array(6),3)),material.clone());tail.renderOrder=40;tail.raycast=()=>{};tail.frustumCulled=false;root.add(tail);
  return {...bucket,line,tail};
 });
 view.viewport.scene.add(root);s.trail={report,root,lines};return s.trail;
}
export function paintMachineTrail(view,report,cursor,world){
 const s=state(view);if(!s.showTrail){if(s.trail)s.trail.root.visible=false;return;}
 const trail=s.trail?.report===report?s.trail:buildTrail(view,report);trail.root.visible=true;
 trail.root.matrix.copy(world[report.support]).multiply(machineMatrix(report.configuration.placement));trail.root.matrixWorldNeedsUpdate=true;
 const point=localTCP(report,world),frame=Math.min(report.frames.length-1,Math.floor(cursor)),current=rapid(report.frames[frame],frame)?0:1;
 trail.lines.forEach((bucket,index)=>{
  bucket.line.visible=bucket.tail.visible=index===0?s.showRapid:s.showCut;
  let lo=0,hi=bucket.ends.length;while(lo<hi){const mid=(lo+hi)>>1;if(bucket.ends[mid]<=cursor)lo=mid+1;else hi=mid;}
  bucket.line.geometry.setDrawRange(0,lo*2);
  const end=bucket.ends[lo];
  bucket.tail.visible&&=index===current&&cursor>0&&end!==undefined&&end>cursor;
  if(bucket.tail.visible){
   const array=bucket.tail.geometry.attributes.position.array;array.set(bucket.positions.slice(lo*6,lo*6+3));array.set(point.toArray(),3);
   bucket.tail.geometry.attributes.position.needsUpdate=true;bucket.tail.computeLineDistances();
  }
 });
}
function observe(context,view,s){
 const viewport=view.viewport,m=selectedMachine(view),objects=viewport?.sceneObjects;
 if(!s.automatic||!m||!objects){for(const o of [...s.faded.keys()])restore(s,o);return;}
 const setup=view.scene.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId),configuration=setup?.machineSetup;
 if(s.tableMachineId!==m.entityId){s.tableMachineId=m.entityId;try{s.tableId=globalThis.localStorage?.getItem(tableKey(m.entityId))||'';}catch{s.tableId='';}}
 const parts=m.elements||[],clear=new Set(parts.filter(p=>p.workSupport).map(p=>p.entityId));
 if(configuration?.machineId===m.entityId&&configuration.supportId)clear.add(configuration.supportId);
 if(parts.some(p=>p.entityId===s.tableId&&p.kind!=='machine'))clear.add(s.tableId);
 // The observation target is always the worktable, independent of selection.
 const children=new Map();for(const p of parts){const ids=children.get(p.parentEntityId)||[];ids.push(p.entityId);children.set(p.parentEntityId,ids);}
 for(const id of clear)for(const child of children.get(id)||[])clear.add(child);
 const tables=new Set(),protectedObjects=new Set(),candidates=new Set();
 const collectTable=o=>{if(!o.visible||o.userData.independentInteractionVisual)return;protectedObjects.add(o);if(o.isMesh&&o.material)tables.add(o);for(const child of o.children)collectTable(child);};
 for(const id of clear){const object=objects.get(id);if(object)collectTable(object);}
 const collect=o=>{if(!o.visible||protectedObjects.has(o)||o.userData.independentInteractionVisual)return;if(o.isMesh&&o.material)candidates.add(o);for(const child of o.children)collect(child);};
 for(const part of parts)if(part.kind!=='machine'&&!clear.has(part.entityId)){const object=objects.get(part.entityId);if(object)collect(object);}
 const camera=viewport.camera;camera.updateMatrixWorld(true);
 const meshes=[...tables,...candidates];for(const object of meshes)object.updateWorldMatrix(true,false);
 const signature=[m.entityId,...camera.matrixWorld.elements,...camera.projectionMatrix.elements,...meshes.flatMap(o=>[o.uuid,o.geometry.uuid,o.geometry.attributes.position.version,o.geometry.index?.version,...o.matrixWorld.elements]),...clear].join(',');
 if(signature===s.signature&&[...s.faded].every(([o,e])=>o.material===e.faded)){for(const o of s.faded.keys())fade(s,o);return;}
 s.signature=signature;
 // Build a shared indirect index once for the actual machine meshes. Neither
 // geometry buffers nor source triangle/face identities are changed.
 s.pickingPrepared??=new WeakMap();for(const object of meshes)if(s.pickingPrepared.get(object)!==object.geometry){try{prepareMeshPicking(object,{minimumTriangles:1000});s.pickingPrepared.set(object,object.geometry);}catch{}}
 const start=performance.now(),canvas=viewport.renderer?.domElement,rect=canvas?.getBoundingClientRect?.();
 const faded=findTableOccluders({camera,tables:[...tables],candidates:[...candidates],width:canvas?.clientWidth||rect?.width||800,height:canvas?.clientHeight||rect?.height||600});
 for(const object of [...s.faded.keys()])if(!faded.has(object))restore(s,object);
 for(const object of faded)fade(s,object);
 s.lastRaycastMs=performance.now()-start;s.lastUpdateMs=s.lastRaycastMs;
}
export function syncMachineObservation(context,view){
 const s=state(view);
 if(view.disposed||!observationActive(view)||context.isCurrentProject?.()===false){disposeMachineObservation(view);return;}
 s.context=context;
 if(!view.fiveMachine?.report||!machinePlaybackActive(view))clearTrail(s);
 if(camMachinePlayback(view)){cancelAnimationFrame(s.animation);s.animation=0;for(const object of [...s.faded.keys()])restore(s,object);return;}
 if(s.animation)return;
 const tick=time=>{
  s.animation=0;
  if(view.disposed||!observationActive(view)||s.context.isCurrentProject?.()===false){disposeMachineObservation(view);return;}
  if(time-s.lastCheck>=80){const start=performance.now();observe(s.context,view,s);s.lastCheck=time;s.lastCheckMs=performance.now()-start;}
  s.animation=requestAnimationFrame(tick);
 };
 s.animation=requestAnimationFrame(tick);
}
