import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {TransformManipulator} from '../../../iCAX-UI/SDK/Viewport/transformManipulator.mjs';
import {addColliderHighlight} from './machineColliderVisuals.mjs';
import {attachColliderBounds} from './machineColliderBounds.mjs';
import {positionWorldTransformHud} from './transformHud.mjs';
import {headColliderState,activeHeadCollider,markHeadColliders,scheduleHeadColliderSave} from './cuttingHeadColliders.mjs';

const valid=b=>[...b.position,...b.rotation,...b.size].every(v=>String(v).trim()&&Number.isFinite(Number(v)))&&b.size.every(v=>Number(v)>0);
function dispose(root){root?.removeFromParent();root?.traverse(o=>{o.geometry?.dispose();for(const m of Array.isArray(o.material)?o.material:[o.material])m?.dispose();});}
function current(preview){const data=activeHeadCollider(preview.view),entry=preview.colliderObjects?.get(data.part?.index+':'+data.box?.id);return entry?{...data,object:entry.wire,parent:entry.parent}:null;}
function repaintFields(preview,box){
 const mount=preview.context.mount;
 for(const key of ['position','rotation','size'])box[key].forEach((v,i)=>{const input=[...(mount?.querySelectorAll('[data-five-head-collider-field]')||[])].find(n=>n.dataset.fiveHeadColliderField===`box:${box.id}:${key}:${i}`);if(input&&input!==document.activeElement)input.value=String(Number(Number(v).toFixed(4)));});
}
export function syncHeadColliderPreview(preview){
 const s=headColliderState(preview.view);if(!preview.model||!s.show){if(preview.colliderRoot)preview.colliderRoot.visible=false;preview.colliderManipulator?.dispose();preview.colliderManipulator=null;return;}
 const signature=JSON.stringify([s.instanceId,s.parts.map(p=>[p.index,p.boxes.map(b=>b.id)])]);
 if(preview.colliderKey!==signature){
  dispose(preview.colliderRoot);preview.colliderRoot=new THREE.Group();preview.colliderRoot.name='Cutting head collision boxes';preview.scene.add(preview.colliderRoot);preview.colliderObjects=new Map();preview.colliderKey=signature;
  for(const part of s.parts){const parent=new THREE.Group();parent.matrixAutoUpdate=false;preview.colliderRoot.add(parent);
   for(const box of part.boxes){const unit=new THREE.BoxGeometry(1,1,1),wire=new THREE.LineSegments(new THREE.EdgesGeometry(unit),new THREE.LineBasicMaterial({color:0x48c98a,depthTest:false,depthWrite:false,transparent:true,opacity:.85,toneMapped:false}));
    wire.renderOrder=800;wire.userData.headColliderId=box.id;wire.userData.headColliderPart=part.index;wire.add(addPickBox(unit,box.id,part.index));unit.dispose();
    const highlight=addColliderHighlight(wire);parent.add(wire);preview.colliderObjects.set(part.index+':'+box.id,{wire,parent,highlight,partIndex:part.index});
   }
  }
 }
 preview.colliderRoot.visible=true;
 for(const part of s.parts){const node=part.index<0?0:part.index,index=preview.motion?.headNodes?.[node];let matrix;
  if(index!=null&&preview.motionWorld?.[index])matrix=preview.motionWorld[index];
  else matrix=preview.head?.assembly?preview.model.children[part.index]?.matrix:preview.model.matrix;
  for(const box of part.boxes){const entry=preview.colliderObjects.get(part.index+':'+box.id);if(!entry||!matrix||!valid(box))continue;
   entry.parent.matrix.copy(matrix);entry.parent.matrixWorldNeedsUpdate=true;
   entry.wire.position.fromArray(box.position.map(Number));entry.wire.rotation.set(...box.rotation.map(v=>Number(v)*Math.PI/180),'ZYX');entry.wire.scale.fromArray(box.size.map(Number));entry.wire.updateMatrixWorld(true);
   const selected=s.active===box.id&&s.partIndex===part.index&&!!s.mode;
   entry.wire.material.color.setHex(selected?0x48c98a:box.enabled?0x50c9dd:0x89969d);entry.wire.material.opacity=selected?1:box.enabled?.32:.2;entry.highlight.visible=selected;
   entry.highlight.children.at(-1).visible=s.mode==='bounds';
  }
 }
 preview.colliderRoot.updateMatrixWorld(true);syncManipulator(preview);
 const data=current(preview);if(data&&s.mode)positionWorldTransformHud({mount:preview.host}, {camera:preview.camera,renderer:preview.renderer},data.object.getWorldPosition(new THREE.Vector3()).toArray(),'head-collider');
}
function addPickBox(unit,id,part){const pick=new THREE.Mesh(unit.clone(),new THREE.MeshBasicMaterial({transparent:true,opacity:0,depthWrite:false}));pick.userData.headColliderId=id;pick.userData.headColliderPart=part;return pick;}
function begin(preview,data){
 const {s,part,box}=data;clearTimeout(s.timer);s.timer=null;s.dragging=true;s.start={box:structuredClone(box),dirty:part.dirty,revision:part.revision,inverse:data.parent.matrixWorld.clone().invert()};
}
function finish(preview,cancel){
 const data=current(preview);if(!data)return;const {s,part,box}=data;
 if(cancel&&s.start){Object.assign(box,s.start.box);part.dirty=s.start.dirty;part.revision=s.start.revision;}
 s.start=null;s.dragging=false;preview.colliderSuppressClick=Date.now()+200;
 const binding=s.binding;if(binding){scheduleHeadColliderSave(binding.context,preview.view,binding.ops,0);binding.ops.renderProject(binding.context,preview.view);}
 preview.renderPreview();
}
function syncManipulator(preview){
 const data=current(preview),mode=data?.s.mode;
 if(!data||!['translate','rotate','scale'].includes(mode)||!valid(data.box)){
  preview.colliderManipulator?.dispose();preview.colliderManipulator=null;return;
 }
 const {s,box}=data;data.parent.updateWorldMatrix(true,false);data.object.updateWorldMatrix(true,false);
 const local=new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...box.rotation.map(v=>Number(v)*Math.PI/180),'ZYX')).setPosition(...box.position.map(Number));
 const world=data.parent.matrixWorld.clone().multiply(local),position=new THREE.Vector3(),quaternion=new THREE.Quaternion(),scale=new THREE.Vector3();world.decompose(position,quaternion,scale);
 preview.colliderViewport??={scene:preview.scene,camera:preview.camera,renderer:preview.renderer};
 preview.colliderManipulator??=new TransformManipulator(preview.colliderViewport,()=>preview.renderPreview());
 preview.colliderManipulator.configure({mode,space:'local',position:position.toArray(),quaternion:quaternion.toArray(),enabled:!preview.view.pending,planeTranslate:true,screenRotate:true,
  onStart:()=>{begin(preview,current(preview));},
  onChange:()=>{
   const data=current(preview);if(!data?.s.start)return;const {s,part,box}=data,proxy=preview.colliderManipulator.proxy;proxy.updateMatrixWorld(true);
   if(mode==='scale')box.size=s.start.box.size.map((v,i)=>Math.max(.001,Number(v)*Math.abs(proxy.scale.getComponent(i))));
   else{const matrix=s.start.inverse.clone().multiply(proxy.matrixWorld),p=new THREE.Vector3(),q=new THREE.Quaternion(),sc=new THREE.Vector3();matrix.decompose(p,q,sc);const r=new THREE.Euler().setFromQuaternion(q,'ZYX');box.position=p.toArray();box.rotation=[r.x,r.y,r.z].map(v=>v*180/Math.PI);}
   markHeadColliders(preview.view,part);repaintFields(preview,box);preview.renderPreview();
  },onEnd:()=>finish(preview,false),onCancel:()=>finish(preview,true),
 });preview.colliderManipulator.update();
}
export function pickHeadCollider(preview,event){
 if(preview.colliderSuppressClick>Date.now())return true;
 const s=headColliderState(preview.view);if(!s.show||!preview.colliderRoot?.visible)return false;
 const rect=preview.canvas.getBoundingClientRect(),caster=new THREE.Raycaster();caster.params.Line.threshold=preview.radius*.007;caster.setFromCamera({x:2*(event.clientX-rect.left)/rect.width-1,y:1-2*(event.clientY-rect.top)/rect.height},preview.camera);
 const hit=caster.intersectObject(preview.colliderRoot,true).find(h=>h.object.userData.headColliderId);if(!hit)return false;
 s.boundsFinish?.(true);s.partIndex=hit.object.userData.headColliderPart;s.active=hit.object.userData.headColliderId;s.mode='bounds';
 if(s.binding)s.binding.ops.renderProject(s.binding.context,preview.view);preview.renderPreview();return true;
}
export function attachHeadColliderPreview(preview){
 const signal=(preview.colliderAbort=new AbortController()).signal,binding={context:preview.context,view:preview.view};
 attachColliderBounds(preview.host,binding,{signal,blocked:view=>view.pending,state:()=>headColliderState(preview.view),viewport:()=>({camera:preview.camera,renderer:preview.renderer}),get:()=>current(preview),
  begin:()=>begin(preview,current(preview)),change:()=>{const {part,box}=current(preview);markHeadColliders(preview.view,part);repaintFields(preview,box);preview.renderPreview();},end:(_,cancel)=>finish(preview,cancel),
 });
 const route=(type,method)=>preview.host.addEventListener(type,event=>{if(event.target!==preview.canvas)return;const manipulator=preview.colliderManipulator;if(manipulator?.[method]?.(event)){event.preventDefault();event.stopImmediatePropagation();}}, {capture:true,signal});
 route('pointerdown','down');route('pointermove','move');route('pointerup','up');
 preview.host.addEventListener('pointercancel',()=>preview.colliderManipulator?.finish(true),{capture:true,signal});
 preview.host.addEventListener('keydown',event=>{
  if(event.defaultPrevented||preview.context.isCurrentProject?.()===false||event.target.closest?.('input,textarea,select,[contenteditable="true"]'))return;
  const s=headColliderState(preview.view);
  if(event.key==='Escape'&&(s.dragging||preview.colliderManipulator?.pointerId!=null)){s.boundsFinish?.(true);preview.colliderManipulator?.finish(true);event.preventDefault();event.stopImmediatePropagation();return;}
  if(!s.mode||!s.show||s.dragging||preview.view.pending||event.repeat||event.isComposing||event.ctrlKey||event.metaKey||event.altKey||event.shiftKey)return;
  const button=[...preview.host.querySelectorAll('[data-transform-owner="head-collider"] button[aria-keyshortcuts]')].find(b=>b.getAttribute('aria-keyshortcuts').toLowerCase()===event.key.toLowerCase()&&!b.disabled);
  if(button){event.preventDefault();event.stopImmediatePropagation();button.click();}
 }, {capture:true,signal});
 preview.focusCollider=()=>focusHeadCollider(preview);
}
function focusHeadCollider(preview){
 const data=current(preview);if(!data)return;data.object.updateWorldMatrix(true,false);const bounds=new THREE.Box3().setFromObject(data.object),center=bounds.getCenter(new THREE.Vector3()),size=bounds.getSize(new THREE.Vector3()).length();
 const direction=preview.camera.position.clone().sub(preview.target).normalize(),distance=Math.max(1,size/(2*Math.tan(THREE.MathUtils.degToRad(preview.camera.fov/2)))*1.25);
 preview.target.copy(center);preview.camera.position.copy(center).addScaledVector(direction,distance);preview.camera.far=Math.max(preview.camera.far,distance*10);preview.camera.updateProjectionMatrix();preview.camera.lookAt(center);preview.renderPreview();
}
export function disposeHeadColliderPreview(preview){
 headColliderState(preview.view).boundsFinish?.(true);preview.colliderAbort?.abort();preview.colliderManipulator?.dispose();preview.colliderManipulator=null;dispose(preview.colliderRoot);preview.colliderRoot=null;preview.colliderObjects=null;preview.colliderKey=null;
}
