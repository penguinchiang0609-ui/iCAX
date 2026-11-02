import {saveEditedProject} from './projectEditPersistence.mjs';
import {renderTransformTools,renderTransformValue,positionTransformHud,positionWorldTransformHud,focusTransformValue} from './transformHud.mjs';
import {placementSnapOptions,updatePlacementSnapFeedback} from './placementSnapping.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {addColliderHighlight} from './machineColliderVisuals.mjs';
import {attachColliderBounds} from './machineColliderBounds.mjs';
import {button,section,hint} from './workspaceControls.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {invalidateMachineCollisionFeedback,machineCollisionBoxMarked} from './machineCollisionFeedback.mjs';
const state=view=>view.fiveMachineColliders??={partId:'',drafts:new Map(),show:true};
const parts=m=>(m?.elements||[]).filter(e=>e.kind!=='machine'&&e.entityId!==m.entityId);
const number=v=>typeof v==='number'?String(Number(v.toFixed(3))):v;
const selectedMachine=view=>view.scene?.machines?.find(m=>m.entityId===view.fiveMachine?.machineId);
const mark=(view,d)=>{invalidateMachineCollisionFeedback(view);d.dirty=true;d.revision=(d.revision||0)+1;if(view.fiveMachine){view.fiveMachine.report=null;view.fiveMachine.snapshot=null;view.fiveMachine.snapshotKey=null;}};
function stopBoundsHud(s){if(s.boundsHudFrame!=null)cancelAnimationFrame(s.boundsHudFrame);s.boundsHudFrame=null;}
export function stopMachineColliderEdit(view){const s=view?.fiveMachineColliders;if(!s)return;stopBoundsHud(s);s.boundsFinish?.(true);s.mode=null;s.hud=null;s.dragging=false;s.start=null;if(s.manipulator&&view.viewport?.transformManipulator===s.manipulator)view.viewport.clearTransformManipulator();s.manipulator=null;}
export function finishMachineColliderField(view){const s=view.fiveMachineColliders,d=s?.drafts.get(s.partId);if(d){d.editKey=null;if(d.dirty&&s.binding)scheduleMachineColliderSave(s.binding.context,view,s.binding.ops,0);}}
const validBoxes=boxes=>!boxes.some(b=>[...b.position,...b.rotation,...b.size].some(v=>!String(v).trim()||!Number.isFinite(Number(v)))||b.size.some(v=>Number(v)<=0));
function saveStatus(view,d){const s=state(view);return d.error||s.fileError||(d.dirty||s.saving?'正在保存…':s.unsavedProject?'已生效，首次保存项目后自动写入文件':'');}
function updateSaveStatus(context,view){if(view.disposed||context.isCurrentProject?.()===false)return;const s=state(view),d=s.drafts.get(s.partId),node=context.mount?.querySelector('[data-box-dirty]');if(node&&d)node.textContent=saveStatus(view,d);const retry=context.mount?.querySelector('[data-five-command="five.machine.box-retry"]');if(retry)retry.hidden=!(d?.error||s.fileError);context.actions?.refreshProjectHistoryControls?.();}
// One queue per project, captured part IDs and immutable snapshots. Native replies
// acknowledge only that snapshot; subsequent edits stay intact. History belongs to the repository.
export function scheduleMachineColliderSave(context,view,ops,delay=300){
 const s=state(view),d=s.drafts.get(s.partId);s.binding={context,ops};if(!d?.dirty||s.dragging)return;
 clearTimeout(d.timer);d.timer=null;d.requested=null;d.error='';
 if(!validBoxes(d.boxes)){d.error='请输入有效数值，尺寸必须大于零。';updateSaveStatus(context,view);return;}
 const snapshot=structuredClone(d.boxes),revision=d.revision,partId=s.partId;
 const enqueue=()=>{d.timer=null;d.requested={snapshot,revision,partId};void drainColliderSaves(context,view).catch(()=>{});};
 if(delay)d.timer=setTimeout(enqueue,delay);else enqueue();updateSaveStatus(context,view);
}
async function drainColliderSaves(context,view){
 const s=state(view);if(s.saving)return s.saving;
 s.saving=(async()=>{await Promise.resolve();
  try{
   for(;;){
    const entry=[...s.drafts.values()].find(d=>d.requested);if(!entry)break;
    const request=entry.requested;entry.requested=null;const {snapshot,revision,partId}=request;
    try{
     const boxes=snapshot.map(b=>({id:b.id,name:b.name,enabled:b.enabled,position:b.position.map(Number),rotationRadians:b.rotation.map(v=>Number(v)*Math.PI/180),size:b.size.map(Number)}));
     const result=await context.sceneProxy.invoke('Machine.SetBoxColliders',{entityId:partId,boxes});
     const attachments=result.machineElement.collisions.map(c=>({...c,entityId:partId}));
     for(const m of view.scene?.machines||[])if(parts(m).some(e=>e.entityId===partId))m.collisions=[...(m.collisions||[]).filter(c=>c.entityId!==partId),...attachments];
     await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
     entry.signature=JSON.stringify(attachments);entry.dirty=entry.revision!==revision;if(!entry.dirty)entry.error='';s.fileDirty=true;
    }catch(e){entry.error='碰撞盒保存失败：'+e.message;throw e;}
   }
   if(s.fileDirty){
    const path=context.projectProxy?.state?.projectPath||context.project?.projectPath;
    s.unsavedProject=!path||path.includes('://');
    if(!s.unsavedProject){try{await saveEditedProject(context);s.fileDirty=false;s.fileError='';}catch(e){s.fileError='项目保存失败：'+e.message;throw e;}}
   }
  }finally{s.saving=null;updateSaveStatus(context,view);if(!view.disposed&&context.isCurrentProject?.()!==false)s.binding?.ops.renderProject(context,view);}
 })();
 try{await s.saving;}finally{if([...s.drafts.values()].some(d=>d.requested))void drainColliderSaves(context,view).catch(()=>{});}
}
export async function flushMachineColliderSaves(context,view){
 const s=view.fiveMachineColliders;if(!s)return;if(s.dragging)throw Error('请先结束碰撞盒拖动。');
 for(const [partId,d] of s.drafts){clearTimeout(d.timer);d.timer=null;if(d.dirty){if(!validBoxes(d.boxes))throw Error('碰撞盒参数无效，尺寸必须大于零。');d.requested={partId,snapshot:structuredClone(d.boxes),revision:d.revision};}}
 do{await drainColliderSaves(context,view);}while(s.saving||[...s.drafts.values()].some(d=>d.requested));
 const failed=[...s.drafts.values()].find(d=>d.dirty);if(failed)throw Error(failed.error||'碰撞盒尚未保存。');
}

function updateColliderFields(context,view,b){
 for(const key of ['position','rotation','size'])b[key].forEach((v,i)=>{const input=context.mount.querySelector(`[data-five-machine-field="box:${b.id}:${key}:${i}"]`);if(input&&input!==document.activeElement)input.value=number(v);});
 const status=context.mount.querySelector('[data-box-dirty]');if(status)status.textContent='调整中…';
}
export function attachMachineColliderBounds(mount,binding){
 const data=view=>{const s=view.fiveMachineColliders,d=current(view,selectedMachine(view)),index=d?.boxes.findIndex(b=>b.id===s.active);return index>=0?{s,d,box:d.boxes[index],object:s.overlay.children[index],parent:s.overlayPart}:null;};
 attachColliderBounds(mount,binding,{
  get:data,
  begin:({view})=>{const {s,d,box}=data(view);clearTimeout(d.timer);d.timer=null;s.dragging=true;s.start={box:structuredClone(box),dirty:d.dirty,revision:d.revision};},
  change:({context,view})=>{const {d,box}=data(view);mark(view,d);syncMachineColliders(view,selectedMachine(view));updateColliderFields(context,view,box);},
  end:({context,view,ops},cancel)=>{const {s,d,box}=data(view);if(cancel){Object.assign(box,s.start.box);d.dirty=s.start.dirty;d.revision=s.start.revision;}s.start=null;s.dragging=false;scheduleMachineColliderSave(context,view,ops,0);syncMachineColliders(view,selectedMachine(view));if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);},
 });
}
export function selectMachineColliderPart(view,id){const s=state(view);if(s.partId===id)return;stopMachineColliderEdit(view);s.partId=id;s.active='';}
function current(view,m){
 const s=state(view);if(!parts(m).some(e=>e.entityId===s.partId))return null;
 const attachments=(m.collisions||[]).filter(c=>c.entityId===s.partId),signature=JSON.stringify(attachments);let d=s.drafts.get(s.partId);
 if(!d||!d.dirty&&d.signature!==signature){d={signature,dirty:false,revision:0,boxes:attachments.filter(c=>c.geometry?.type==='box').map((c,index)=>({id:c.id||d?.boxes[index]?.id||crypto.randomUUID(),sourceAttachmentIndex:attachments.indexOf(c),name:c.name||c.collisionName||'碰撞盒',enabled:c.enabled!==false,position:c.pose.slice(0,3).map(v=>v*1000),rotation:c.pose.slice(3).map(v=>v*180/Math.PI),size:c.geometry.size.map(v=>v*1000)}))};s.drafts.set(s.partId,d);}
 if(!d.boxes.some(b=>b.id===s.active))s.active=d.boxes[0]?.id||'';return d;
}
export function renderMachineColliders(view,m,busy=''){
 if(!m||state(view).partId!==view.fiveMachineSelectedPartId)return '';const s=state(view),d=current(view,m);if(!d)return '';
 const configuration=view.scene?.domain?.setups?.find(setup=>setup.id===view.scene.domain.activeSetupId)?.machineSetup;
 const tool=configuration?.machineId===m.entityId&&configuration.toolId===s.partId;
 const roleHint=tool?hint('切割头直接采用此机床实例启用的碰撞盒。编辑会影响空移规划、CAM 碰撞检测和机床仿真。'):'';
 const field=(b,key,label,unit)=>`<div class="five-collider-vector" data-five-key="box-row-${b.id}-${key}"><span>${label}<small>${unit}</small></span>${b[key].map((v,i)=>`<label data-five-key="box-${b.id}-${key}-${i}"><span>${'XYZ'[i]}</span><input type="text" inputmode="decimal" data-five-key="box-input-${b.id}-${key}-${i}" data-five-machine-field="box:${b.id}:${key}:${i}" aria-label="${attr(b.name)} ${label} ${'XYZ'[i]}" value="${attr(number(v))}" ${busy||s.active!==b.id||!s.mode?'disabled':''}></label>`).join('')}</div>`;
 const cards=d.boxes.map((b,index)=>{const editing=s.active===b.id&&!!s.mode,collision=machineCollisionBoxMarked(view,s.partId,b.sourceAttachmentIndex??index,b.id);return `<article class="five-collider-card" data-five-key="box-card-${b.id}" data-box-id="${b.id}" data-editing="${editing}" data-enabled="${b.enabled}" data-collision="${collision}">
 <header><label><input type="checkbox" data-five-key="box-enabled-${b.id}" data-five-machine-field="box:${b.id}:enabled" aria-label="${attr(b.name)} 参与碰撞检测" ${b.enabled?'checked':''} ${busy}><strong title="${attr(b.name)}">${esc(b.name)}</strong></label><span class="five-collider-header-tools">${button(editing?'five.machine.box-edit-end':'five.machine.box-edit:'+b.id+':bounds',editing?'完成':'编辑',`${busy} data-five-key="box-edit-toggle-${attr(b.id)}" aria-label="${editing?'完成碰撞盒编辑':'编辑碰撞盒'}" aria-expanded="${editing}" aria-controls="box-body-${attr(b.id)}" aria-pressed="${editing}"`)}${button('five.machine.box-remove:'+b.id,'×',`${busy||d.boxes.length<=1?'disabled':''} aria-label="删除 ${attr(b.name)}" title="删除碰撞盒"`)}</span></header>
 <div class="five-collider-body" id="box-body-${attr(b.id)}" data-five-key="box-body-${b.id}" ${editing?'':'hidden'}>
 <input class="five-collider-name" data-five-key="box-name-${b.id}" data-five-machine-field="box:${b.id}:name" aria-label="碰撞盒名称" value="${attr(b.name)}" ${busy||s.active!==b.id||!s.mode?'disabled':''}>
 ${field(b,'position','中心偏移','mm')}${field(b,'rotation','旋转','°')}${field(b,'size','尺寸','mm')}
 </div></article>`;}).join('');
 return section('machine-colliders','碰撞盒',roleHint+`<div class="five-collider-toolbar"><label><input type="checkbox" data-five-machine-field="box:show" ${s.show?'checked':''}>显示线框</label>${button('five.machine.box-add','＋ 添加',busy)}</div>`+(s.mode?hint(s.mode==='bounds'?'拖动面的方块调整边界 · Ctrl 按 1 mm 吸附 · F 聚焦':'相对当前部件原点 · F 聚焦'):'')+cards+(!d.boxes.length?hint('当前部件没有碰撞盒。'):'')+`<small data-box-dirty role="status">${esc(saveStatus(view,d))}</small>`+button('five.machine.box-retry','重试保存',d.error||s.fileError?'':'hidden'));
}
function clearOverlay(view){const s=view?.fiveMachineColliders;if(!s?.overlay)return;s.overlay.removeFromParent();s.overlay.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});s.overlay=null;s.overlayPart=null;}
export function disposeMachineColliders(view){stopMachineColliderEdit(view);clearOverlay(view);}
export function focusMachineCollider(view,id){
 const s=view.fiveMachineColliders,vp=view.viewport,d=current(view,selectedMachine(view));
 const index=d?.boxes.findIndex(b=>b.id===id),object=s?.overlay?.children[index];
 if(!object?.visible||!vp)return false;
 object.updateWorldMatrix(true,false);vp.camera.updateMatrixWorld(true);
 const center=object.getWorldPosition(new THREE.Vector3()),inverseViewRotation=vp.camera.quaternion.clone().invert();
 let halfWidth=0,halfHeight=0,halfDepth=0;
 // Fit only this box's eight world-space corners, including its owner's
 // complete joint chain. Keep the user's view direction and projection.
 for(const x of [-.5,.5])for(const y of [-.5,.5])for(const z of [-.5,.5]){
  const p=new THREE.Vector3(x,y,z).applyMatrix4(object.matrixWorld).sub(center).applyQuaternion(inverseViewRotation);
  halfWidth=Math.max(halfWidth,Math.abs(p.x));halfHeight=Math.max(halfHeight,Math.abs(p.y));halfDepth=Math.max(halfDepth,Math.abs(p.z));
 }
 const rect=vp.renderer.domElement.getBoundingClientRect();if(!rect.width||!rect.height)return false;
 const tanHalfFov=Math.tan(THREE.MathUtils.degToRad(vp.perspectiveFov*.5)),aspect=rect.width/rect.height;
 const radius=Math.max(1,1.5*Math.max(halfHeight/tanHalfFov,halfWidth/(tanHalfFov*aspect))+halfDepth);
 if(![...center.toArray(),radius].every(Number.isFinite))return false;
 vp.camera.near=Math.max(.001,radius/10000);vp.camera.far=Math.max(vp.camera.far,radius*20);
 vp.setCameraState({...vp.getCameraState(),target:{x:center.x,y:center.y,z:center.z},radius});
 return true;
}
export function syncMachineColliders(view,m,visible=true){
 const s=state(view);if(!visible||!m||!s.show||s.partId!==view.fiveMachineSelectedPartId){disposeMachineColliders(view);return;}const d=current(view,m),parent=view.viewport?.sceneObjects?.get(s.partId);if(!d||!parent){disposeMachineColliders(view);return;}
 if(s.overlayPart!==parent||s.overlay?.children.length!==d.boxes.length){clearOverlay(view);s.overlay=new THREE.Group();s.overlay.userData.machineColliderPreview=true;s.overlayPart=parent;
  for(const b of d.boxes){const unit=new THREE.BoxGeometry(1,1,1),edges=new THREE.EdgesGeometry(unit);unit.dispose();const o=new THREE.LineSegments(edges,new THREE.LineBasicMaterial({color:0x48c98a,depthTest:false,depthWrite:false,transparent:true,opacity:.85,toneMapped:false}));o.userData.independentInteractionVisual=true;o.renderOrder=900;o.raycast=()=>{};addColliderHighlight(o);s.overlay.add(o);}parent.add(s.overlay);}
 d.boxes.forEach((stored,i)=>{const b=s.hud?.id===stored.id&&s.hud.candidate||stored;const values=[...b.position,...b.rotation,...b.size].map(Number),o=s.overlay.children[i],selected=b.id===s.active&&!!s.mode;o.visible=values.every(Number.isFinite)&&b.size.every(v=>Number(v)>0);if(!o.visible)return;o.position.set(...b.position.map(Number));o.rotation.set(...b.rotation.map(v=>Number(v)*Math.PI/180),'ZYX');o.scale.set(...b.size.map(Number));o.material.color.set(selected?0x48c98a:!b.enabled?0x89969d:0x50c9dd);o.material.opacity=selected?1:(b.enabled?.32:.2);o.children[0].visible=selected;o.children[0].children[3].visible=selected&&s.mode==='bounds';});
}
export function machineColliderField(view,m,key,value){
 if(!key.startsWith('box:'))return false;const s=state(view),d=current(view,m);if(!d)return true;
 if(key==='box:show'){s.show=value;if(!value)stopMachineColliderEdit(view);}
 else {s.hud=null;const [,id,group,index]=key.split(':'),b=d.boxes.find(b=>b.id===id);if(b){if(group!=='enabled'&&(!s.mode||s.active!==id))return true;if(d.editKey!==key||group==='enabled'){d.editKey=key;}if(group==='enabled')b.enabled=!!value;else if(group==='name')b.name=value;else if(['position','rotation','size'].includes(group)&&Number(index)>=0&&Number(index)<3)b[group][Number(index)]=value;mark(view,d);}}
 syncMachineColliders(view,m);return true;
}
const hudNumber=v=>String(Math.round(v*1e6)/1e6);
function colliderValueFields(h){
 return (h.mode==='translate'&&h.axis.length>1||h.mode==='scale'&&h.axis.length===2)?[...h.axis].map(axis=>({name:axis,label:axis.toUpperCase(),value:h.values['xyz'.indexOf(axis)],aria:axis.toUpperCase()+(h.mode==='scale'?' 缩放比例':' 位移')})):[{name:'value',label:h.axis==='e'?'视图':h.axis.toUpperCase(),value:h.value}];
}
export function renderMachineColliderHud(view){
 const s=view.fiveMachineColliders;if(view.fiveAxisArea!=='simulation'||activeTool(view)!=='machine-instances'||!s?.mode||!s.show)return '';
 const busy=view.pending||view.fiveMachine?.busy,modes=[['translate','平移'],['rotate','旋转'],['scale','缩放'],['bounds','边界']].map(([mode,label])=>[mode,label,'five.machine.box-edit:'+s.active+':'+mode]);modes.push(['focus','聚焦','five.machine.box-focus']);
 const tools=renderTransformTools({owner:'collider',title:'碰撞盒变换',mode:s.mode,busy,modes}),h=s.mode==='bounds'?null:s.hud;
 return tools+(!h?'':renderTransformValue({owner:'collider',mode:h.mode,axis:h.axis,fields:colliderValueFields(h),attribute:'data-five-collider-value',busy:s.dragging||busy,error:h.error}));
}
function colliderNumericBox(h){
 const amount=value=>{if(!String(value).trim()||!Number.isFinite(Number(value)))throw Error('请输入有效数值。');return Number(value);};
 const box=structuredClone(h.baseBox),world=new THREE.Matrix4().fromArray(h.world),q=new THREE.Quaternion().setFromRotationMatrix(world);
 if(h.mode==='scale'){
  box.size=box.size.map((v,i)=>{if(!h.axis.includes('xyz'[i]))return Number(v);const factor=amount(h.axis.length===2?h.values[i]:h.value);if(factor<=0)throw Error('缩放比例必须大于零。');return Number(v)*factor;});return box;
 }
 if(h.mode==='translate'){
  const delta=new THREE.Vector3(...['x','y','z'].map((axis,i)=>h.axis.includes(axis)?amount(h.axis.length>1?h.values[i]:h.value):0));
  if(h.space==='local')delta.applyQuaternion(q);world.setPosition(new THREE.Vector3().setFromMatrixPosition(world).add(delta));
 }else{
  const axis=h.axis==='e'?new THREE.Vector3(...h.direction):new THREE.Vector3().setComponent('xyz'.indexOf(h.axis),1);
  if(h.axis!=='e'&&h.space==='local')axis.applyQuaternion(q);
  const rotated=new THREE.Quaternion().setFromAxisAngle(axis,amount(h.value)*Math.PI/180).multiply(q);
  world.compose(new THREE.Vector3().setFromMatrixPosition(world),rotated,new THREE.Vector3(1,1,1));
 }
 const local=new THREE.Matrix4().fromArray(h.inverse).multiply(world),p=new THREE.Vector3(),rotation=new THREE.Quaternion(),scale=new THREE.Vector3();local.decompose(p,rotation,scale);
 const e=new THREE.Euler().setFromQuaternion(rotation,'ZYX');box.position=p.toArray();box.rotation=[e.x,e.y,e.z].map(v=>v*180/Math.PI);return box;
}
export function machineColliderValueChanged(context,view,name,value,ops){
 const s=view.fiveMachineColliders,h=s?.hud;if(!h||s.dragging)return;
 if(name==='value')h.value=value;else{const i='xyz'.indexOf(name);if(i<0||!h.axis.includes(name))return;h.values[i]=value;}
 try{h.candidate=colliderNumericBox(h);h.error='';}catch(e){h.candidate=null;h.error=e.message;}
 syncMachineColliders(view,selectedMachine(view));ops.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();
}
export function cancelMachineColliderValue(context,view,ops){
 const s=view.fiveMachineColliders;if(!s)return;s.hud=null;syncMachineColliders(view,selectedMachine(view));ops.renderProject(context,view);
}
export function machineColliderSpaceChanged(view){const s=view.fiveMachineColliders;if(s)s.hud=null;}
export async function machineColliderCommand(context,view,m,action,ops){
 if(!action.startsWith('box-'))return false;const s=state(view),d=current(view,m);if(!d)return true;
 if(action==='box-value-apply'){const h=s.hud,b=d.boxes.find(b=>b.id===h?.id);if(!h||!b||!h.candidate&&!h.error)return true;try{const next=colliderNumericBox(h);if(JSON.stringify(next)!==JSON.stringify(b)){Object.assign(b,next);mark(view,d);}h.candidate=null;h.error='';}catch(e){h.error=e.message;}}
 else if(action==='box-value-cancel'){s.hud=null;}
 else if(action==='box-edit-end')stopMachineColliderEdit(view);
 else if(action==='box-focus')focusMachineCollider(view,s.active);

 else if(action.startsWith('box-edit:')){const [,id,mode]=action.split(':');if(d.boxes.some(b=>b.id===id)&&['translate','rotate','scale','bounds'].includes(mode)){if(s.mode!==mode||s.active!==id)s.hud=null;s.active=id;s.mode=mode;s.show=true;}}
 else if(action==='box-add'){stopMachineColliderEdit(view);const geometry=view.viewport?.sceneObjects?.get(s.partId)?.geometry;geometry?.computeBoundingBox();const bounds=geometry?.boundingBox,center=new THREE.Vector3(),size=new THREE.Vector3(100,100,100);if(bounds){bounds.getCenter(center);bounds.getSize(size);}const b={id:crypto.randomUUID(),name:'碰撞盒 '+(d.boxes.length+1),enabled:true,position:center.toArray(),rotation:[0,0,0],size:size.toArray().map(v=>Math.max(.1,v))};d.boxes.push(b);s.active=b.id;mark(view,d);}
 else if(action.startsWith('box-remove:')&&d.boxes.length>1){const id=action.slice(11);stopMachineColliderEdit(view);d.boxes=d.boxes.filter(b=>b.id!==id);mark(view,d);}
 else if(action==='box-retry')await flushMachineColliderSaves(context,view);
 if(d.dirty)scheduleMachineColliderSave(context,view,ops,0);
 syncMachineColliders(view,m);ops.renderProject(context,view);return true;
}
function followBoundsHud(context,view){
 const s=view.fiveMachineColliders;stopBoundsHud(s);
 const update=()=>{
  s.boundsHudFrame=null;
  if(view.disposed||context.isCurrentProject?.()===false||s.mode!=='bounds'||!s.show||view.fiveAxisArea!=='simulation'||activeTool(view)!=='machine-instances'||s.partId!==view.fiveMachineSelectedPartId)return;
  const index=s.drafts.get(s.partId)?.boxes.findIndex(b=>b.id===s.active),object=s.overlay?.children[index];if(!object)return;
  positionWorldTransformHud(context,view.viewport,object.getWorldPosition(new THREE.Vector3()).toArray(),'collider');
  s.boundsHudFrame=requestAnimationFrame(update);
 };
 update();
}
export function syncMachineColliderEdit(context,view,ops){
 const s=view.fiveMachineColliders;if(!s?.mode)return;const m=selectedMachine(view),d=current(view,m),b=d?.boxes.find(b=>b.id===s.active),vp=view.viewport,parent=vp?.sceneObjects.get(s.partId);
 if(view.fiveAxisArea!=='simulation'||activeTool(view)!=='machine-instances'||s.partId!==view.fiveMachineSelectedPartId||!b||!parent||!s.show){stopMachineColliderEdit(view);return;}
 if(s.mode==='bounds'){if(s.manipulator&&vp.transformManipulator===s.manipulator)vp.clearTransformManipulator();s.manipulator=null;followBoundsHud(context,view);return;}
 stopBoundsHud(s);
 const values=[...b.position,...b.rotation,...b.size].map(Number);if(!values.every(Number.isFinite)||b.size.some(v=>Number(v)<=0)){stopMachineColliderEdit(view);return;}
 const shown=s.hud?.id===b.id&&s.hud.candidate||b;
 parent.updateWorldMatrix(true,false);const local=new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...shown.rotation.map(v=>Number(v)*Math.PI/180),'ZYX'));local.setPosition(...shown.position.map(Number));const world=parent.matrixWorld.clone().multiply(local),position=new THREE.Vector3(),rotation=new THREE.Quaternion(),scale=new THREE.Vector3();world.decompose(position,rotation,scale);
 const render=()=>{if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);};
 vp.setTransformManipulator({mode:s.mode,space:view.fivePlacementSpace||'world',activeAxis:s.hud?.axis,position:position.toArray(),quaternion:rotation.toArray(),planeTranslate:true,screenRotate:true,snap:placementSnapOptions(vp,position.toArray()),onPosition:p=>positionTransformHud(context,p,'collider'),enabled:!view.pending&&!view.fiveMachine?.busy&&!view.fiveMachineJog?.running,
  onStart:({axis,direction})=>{s.hud=null;clearTimeout(d.timer);d.timer=null;s.dragging=true;s.start={box:structuredClone(b),inverse:parent.matrixWorld.clone().invert(),dirty:d.dirty,revision:d.revision};const base=new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...b.rotation.map(v=>Number(v)*Math.PI/180),'ZYX')).setPosition(...b.position.map(Number));s.hud={id:b.id,mode:s.mode,axis:axis.toLowerCase(),direction,space:view.fivePlacementSpace||'world',world:parent.matrixWorld.clone().multiply(base).toArray(),inverse:parent.matrixWorld.clone().invert().toArray(),baseBox:structuredClone(b),value:s.mode==='scale'?'1':'0',values:Array(3).fill(s.mode==='scale'?'1':'0')};render();},
  onChange:({translation,angle,scale,snap})=>{
   if(!s.dragging||!s.start)return;const proxy=vp.transformManipulator.proxy;proxy.updateMatrixWorld(true);
   if(s.mode==='scale'){b.size=s.start.box.size.map((v,i)=>Math.max(.001,Math.min(1e7,Number(v)*Math.abs(proxy.scale.getComponent(i)))));}
   else{const local=s.start.inverse.clone().multiply(proxy.matrixWorld),p=new THREE.Vector3(),q=new THREE.Quaternion(),sc=new THREE.Vector3();local.decompose(p,q,sc);const e=new THREE.Euler().setFromQuaternion(q,'ZYX');b.position=p.toArray();b.rotation=[e.x,e.y,e.z].map(v=>v*180/Math.PI);}
   const h=s.hud;if(h){if(s.mode==='translate'){const delta=new THREE.Vector3(...translation);if(h.space==='local')delta.applyQuaternion(new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().fromArray(h.world)).invert());h.values=delta.toArray().map(hudNumber);h.value=h.values['xyz'.indexOf(h.axis)]||'0';}else {h.value=hudNumber(s.mode==='rotate'?angle*180/Math.PI:scale['xyz'.indexOf(h.axis[0])]);if(s.mode==='scale')h.values=scale.map(hudNumber);}for(const f of colliderValueFields(h)){const input=context.mount.querySelector(`[data-five-collider-value="${f.name}"]`);if(input)input.value=f.value;}}updatePlacementSnapFeedback(context,snap);
   mark(view,d);syncMachineColliders(view,m);
   updateColliderFields(context,view,b);
  },
  onEnd:()=>{s.dragging=false;s.start=null;scheduleMachineColliderSave(context,view,ops,0);render();focusTransformValue(context,'data-five-collider-value');},
  onCancel:()=>{s.hud=null;if(s.start){Object.assign(b,s.start.box);d.dirty=s.start.dirty;d.revision=s.start.revision;}s.dragging=false;s.start=null;scheduleMachineColliderSave(context,view,ops,0);syncMachineColliders(view,m);render();}
 });s.manipulator=vp.transformManipulator;
}
export function machineCollidersDirty(view,m){const ids=new Set(parts(m).map(e=>e.entityId));return [...(view.fiveMachineColliders?.drafts||[])].some(([id,d])=>ids.has(id)&&d.dirty);}
