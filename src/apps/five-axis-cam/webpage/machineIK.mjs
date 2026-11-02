import {saveEditedProject} from './projectEditPersistence.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {activeTool} from './workspaceModel.mjs';
import {button,section,hint} from './workspaceControls.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {flushMachineColliderSaves,stopMachineColliderEdit} from './machineColliders.mjs';
import {renderTransformTools,renderTransformValue,positionTransformHud} from './transformHud.mjs';
import {MachineIKPDOStream} from './machineIKPDOStream.mjs';
import {machinePoseFrame,machinePoseValues,machinePoseTarget} from './machinePoseCoordinates.mjs';
import {machineAxisTarget,machineAxisValue} from './machineAxisTarget.mjs';
import {setMachineCollisionFeedback,syncMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature} from './machineCollisionFeedback.mjs';

const reader=view=>view.areas?.[view.activeAreaId]?.viewReader;
const machine=view=>view.scene?.machines?.find(m=>m.entityId===view.fiveMachine?.machineId);
function path(m,id){const ids=[];for(let e=m?.elements?.find(e=>e.entityId===id);e&&!ids.includes(e.entityId);e=m.elements.find(p=>p.entityId===e.parentEntityId))ids.push(e.entityId);return ids;}
const movable=(m,id)=>path(m,id).some(id=>m.joints?.some(j=>j.entityId===id&&j.type!=='fixed'));
export const canAdjustMachinePart=view=>movable(machine(view),view.fiveMachineSelectedPartId);
const rotatable=(m,tip)=>path(m,tip).some(id=>m.joints?.some(j=>j.entityId===id&&['revolute','continuous'].includes(j.type)&&(!Number.isFinite(j.lower)||!Number.isFinite(j.upper)||j.upper>j.lower)));
export const canRotateMachinePart=view=>rotatable(machine(view),view.fiveMachineSelectedPartId);
const ikMode=(view,preferred)=>preferred==='rotate'&&canRotateMachinePart(view)?'rotate':'translate';
export const selectedMachineJoint=view=>machine(view)?.joints?.find(j=>j.entityId===view.fiveMachineSelectedPartId&&['revolute','continuous','prismatic'].includes(j.type));
export function selectMachinePoseTarget(view){
 const s=view.fiveMachineIK??={},m=machine(view),id=view.fiveMachineSelectedPartId;
 if(!movable(m,id)){disposeMachineIK(view);return;}
 s.tip=id;s.machineId=m.entityId;s.pose=null;s.message='';s.hud=null;
 s.motionMode=s.preference==='IK'||!selectedMachineJoint(view)?'IK':'FK';
 s.mode=s.motionMode==='FK'?(selectedMachineJoint(view).type==='prismatic'?'translate':'rotate'):ikMode(view,s.ikMode);
}
const rounded=value=>String(Number(Number(value).toFixed(3)));
const row=matrix=>[0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15].map(i=>matrix.elements[i]);
const acceptedPose=result=>['Ready','Approximated'].includes(result.status)&&result.collisionStatus==='Clear';
const poseFeedback=result=>result.status==='Approximated'?(result.reason||'已到达当前可达边界。'):'';
function actualPose(result){return result.tipWorld?.length===16&&Array.from(result.tipWorld).every(Number.isFinite)?new THREE.Matrix4().set(...result.tipWorld):null;}
function acceptAxisValue(view,result,tip){
 const h=view.fiveMachineIK?.hud,actual=actualPose(result);if(!h||!actual||tip&&h.tip!==tip)return;
 h.value=rounded(machineAxisValue(h.baseWorld,h.direction,h.mode,actual,h.acceptedValue??0));h.acceptedValue=h.value;
}
function sequence(value){try{return BigInt(value?.mutationSequence??-1);}catch{return -1n;}}
function poseExpected(view){
 const current=view.scene.domain.expected,receipt=view.fiveMachineIK?.receiptExpected;
 return receipt?.epoch===current.epoch&&sequence(receipt)>sequence(current)?receipt:current;
}
function acceptPoseExpected(view,expected){
 if(!expected||expected.epoch!==view.scene?.domain?.expected?.epoch)return;
 const s=view.fiveMachineIK,current=poseExpected(view);
 if(sequence(expected)<sequence(current))return;
 s.receiptExpected={...expected};view.scene.domain.expected={...expected};
}
function clearUnavailableRotationDraft(view,p){
 const m=view.scene?.machines?.find(m=>m.entityId===p.machineId);
 if(rotatable(m,p.tip)||![...p.edited].some(i=>i>=3))return;
 const object=view.viewport?.sceneObjects.get(p.tip);if(!object)return;
 object.updateWorldMatrix(true,false);const angles=machinePoseValues(object.matrixWorld,p.frame).slice(3);
 clearTimeout(p.timer);p.timer=null;p.requested=null;p.revision++;p.error='';
 for(let i=3;i<6;i++){p.values[i]=angles[i-3];p.edited.delete(i);}
 if(Number(p.editKey?.slice(8))>=3)p.editKey=null;
 p.dirty=p.dirty&&[...p.edited].some(i=>i<3);
 if(p.hudEditing){for(let i=3;i<6;i++){p.hudEditing.values[i]=angles[i-3];p.hudEditing.edited.delete(i);}p.hudEditing.error='';p.hudEditing.dirty=p.hudEditing.dirty&&[...p.hudEditing.edited].some(i=>i<3);}
 const binding=view.fiveMachineIK.binding;if(p.dirty&&binding)schedulePoseEdit(binding.context,view,p,binding.ops);
}
function poseDraft(view){
 const m=machine(view),tip=view.fiveMachineSelectedPartId,s=view.fiveMachineIK??={},key=m?.entityId+':'+tip;
 s.drafts??=new Map();const existing=s.drafts.get(key);if(existing)clearUnavailableRotationDraft(view,existing);if(existing&&(existing.dirty||existing.editKey||existing.requested||existing.saving||existing.hudEditing))return s.pose=existing;
 const object=view.viewport?.sceneObjects.get(tip);if(!object){s.pose=null;return null;}
 const space=view.fivePlacementSpace||'world',frame=s.gesture?.frame||machinePoseFrame(view,m,tip,space);if(!frame){s.pose=null;return null;}
 object.updateWorldMatrix(true,false);
 const draft={key,tip,machineId:m.entityId,space,frame,revision:0,values:machinePoseValues(object.matrixWorld,frame),dirty:false,edited:new Set()};s.drafts.set(key,draft);return s.pose=draft;
}
export function machineIKField(context,view,key,value,ops){
 if(key==='ik-delta'){
  const s=view.fiveMachineIK,h=s?.hud;
  if(!h||s.spaceChanging||s.dragging||s.inflight||s.editTask||s.finishing||!beginMachinePoseHudEdit(view))return true;
  const p=s.pose;h.value=value;p.editKey=key;p.dirty=true;p.revision++;
  try{p.values=machinePoseValues(machineAxisTarget(h.baseWorld,h.direction,h.mode,value),p.frame);for(const i of h.mode==='translate'?[0,1,2]:[3,4,5])p.edited.add(i);schedulePoseEdit(context,view,p,ops);}
  catch{clearTimeout(p.timer);p.timer=null;p.requested=null;p.error='请输入有效的'+(h.mode==='translate'?'位移。':'角度。');}
  renderPose(context,view,ops);return true;
 }
 if(!key.startsWith('ik-pose:')){if((key.startsWith('joint:')||key.startsWith('origin:'))&&view.fiveMachineIK)view.fiveMachineIK.hud=null;return false;}
 if(view.fiveMachineIK?.spaceChanging)return true;
 if(Number(key.slice(8))>=3&&!canRotateMachinePart(view))return true;
 if(view.fiveMachineIK)view.fiveMachineIK.hud=null;
 stopMachineColliderEdit(view);
 const p=poseDraft(view),index=Number(key.slice(8));if(p&&index>=0&&index<6&&canAdjustMachinePart(view)){if(p.editKey!==key){p.editKey=key;}p.values[index]=value;p.edited.add(index);p.dirty=true;p.revision++;schedulePoseEdit(context,view,p,ops);if(!p.error)renderPose(context,view,ops);}
 return true;
}
function renderPose(context,view,ops){if(!view.disposed&&context.isCurrentProject?.()!==false){ops?.renderProject(context,view);context.actions?.refreshProjectHistoryControls?.();}}
function savePoseProject(context,view,ops){
 const s=view.fiveMachineIK;s.fileDirty=true;if(s.fileSaving)return s.fileSaving;
 s.fileSaving=(async()=>{await Promise.resolve();try{
  while(s.fileDirty){s.fileDirty=false;const path=context.projectProxy?.state?.projectPath||context.project?.projectPath;
   if(!path||path.includes('://')){s.fileMessage='已生效，首次保存项目后自动写入文件';break;}
   await saveEditedProject(context);s.fileMessage='';
  }
 }catch(e){s.fileDirty=true;s.fileMessage='项目保存失败：'+e.message;}finally{s.fileSaving=null;renderPose(context,view,ops);}})();return s.fileSaving;
}
function schedulePoseEdit(context,view,p,ops,delay=300){
 const s=view.fiveMachineIK;s.binding={context,ops};clearTimeout(p.timer);p.timer=null;p.requested=null;p.error='';
 const values=p.values.map(v=>String(v).trim()===''?NaN:Number(v));
 if(!values.every(Number.isFinite)){p.error='位置与空间角必须填写有效数值。';renderPose(context,view,ops);return;}
 if(p.hudEditing)return;
 const request={values,target:row(machinePoseTarget(values,p.frame)),revision:p.revision};
 const enqueue=()=>{p.timer=null;p.requested=request;void drainPoseEdits(context,view,ops);};
 if(delay)p.timer=setTimeout(enqueue,delay);else enqueue();
}
async function drainPoseEdits(context,view,ops){
 const s=view.fiveMachineIK;if(s.editTask)return s.editTask;
 s.editTask=(async()=>{await Promise.resolve();try{
  await s.inflight;
  for(;;){const p=[...(s.drafts?.values()||[])].find(p=>p.requested);if(!p)break;
   const request=p.requested;p.requested=null;p.saving=true;
   try{
    if(s.gesture){if(s.dragging)throw Error('请先结束部件拖动。');await finishMachineIK(view);if(s.gesture)throw Error(s.message||'上一次部件拖动尚未结束。');}
    const m=view.scene.machines.find(m=>m.entityId===p.machineId);if(!m||m.enabled===false)throw Error('该机床实例未启用。');
    s.inflight=applyTarget(context,view,m,{tip:p.tip},request.target);const result=await s.inflight;
    if(p.revision===request.revision){const actual=actualPose(result);if(actual){p.values=machinePoseValues(actual,p.frame);p.edited.clear();}acceptAxisValue(view,result,p.tip);p.dirty=false;p.error='';delete p.confirmingHud;}
    void savePoseProject(context,view,ops);
   }catch(e){if(p.revision===request.revision){p.error=e.message;if(p.confirmingHud){p.hudEditing=p.confirmingHud;delete p.confirmingHud;}}}
   finally{p.saving=false;s.inflight=null;renderPose(context,view,ops);}
  }
 }finally{s.editTask=null;renderPose(context,view,ops);}})();return s.editTask;
}
export function finishMachinePoseField(context,view,key,ops){
 const s=view.fiveMachineIK,p=s?.pose;if(!p||p.editKey!==key)return;
 p.editKey=null;if(p.dirty&&!p.error)schedulePoseEdit(context,view,p,ops,0);
}
export async function flushMachinePoseEdits(context,view,ops){
 const s=view?.fiveMachineIK;if(!s)return;if(s.dragging)throw Error('请先结束部件拖动。');
 if(s.pose?.hudEditing)throw Error('请先确认或取消浮动输入。');
 for(const p of s.drafts?.values()||[]){clearUnavailableRotationDraft(view,p);if(p.dirty){if(p.error)throw Error(p.error);schedulePoseEdit(context,view,p,ops,0);}}
 await s.editTask;await finishMachineIK(view);if(s.dragging||s.pointer&&!s.pointer.ended)throw Error('请先结束部件拖动。');if(s.gesture)throw Error(s.message||'部件拖动尚未完成保存。');await s.fileSaving;if(s.dragging||s.pointer&&!s.pointer.ended)throw Error('请先结束部件拖动。');
 const failed=[...(s.drafts?.values()||[])].find(p=>p.dirty);if(failed)throw Error(failed.error||'部件位姿尚未保存。');
 if(s.fileDirty){await savePoseProject(context,view,ops);if(s.fileDirty)throw Error(s.fileMessage);}
}
export async function machinePoseSpaceChanged(context,view,space,ops){
 const s=view.fiveMachineIK??={},previous=view.fivePlacementSpace||'world';
 if(s.spaceChanging){s.nextSpace=space;view.viewport?.setTransformSpace(previous);return;}
 if(space===previous)return;
 view.viewport?.setTransformSpace(previous);
 if(s.pose?.hudEditing){if(s.pose.revision!==s.pose.hudEditing.revision)throw Error('请先确认或取消浮动输入，再切换坐标系。');finishMachinePoseHudEdit(context,view,true,ops);}
 s.nextSpace=space;s.spaceChanging=true;
 try{
  renderPose(context,view,ops);await finishMachineIK(view);await flushMachinePoseEdits(context,view,ops);
  if(view.disposed||context.isCurrentProject?.()===false)return;
  const m=machine(view),tip=view.fiveMachineSelectedPartId,next=s.nextSpace;
  if(m&&tip&&!machinePoseFrame(view,m,tip,next))throw Error('部件的父坐标尚未就绪，请刷新后重试。');
  view.fivePlacementSpace=next;view.viewport?.setTransformSpace(next);
  s.pose=null;s.hud=null;s.drafts?.clear();delete s.transformTarget;
 }finally{s.spaceChanging=false;delete s.nextSpace;renderPose(context,view,ops);}
}
export function beginMachinePoseHudEdit(view){
 const s=view.fiveMachineIK,p=poseDraft(view);if(!p||s.dragging||s.inflight||s.editTask||s.finishing)return false;
 if(p.hudEditing)return true;clearTimeout(p.timer);p.timer=null;p.requested=null;
 p.hudEditing={values:[...p.values],dirty:p.dirty,error:p.error,revision:p.revision,edited:new Set(p.edited),hudValue:s.hud?.value};return true;
}
export function finishMachinePoseHudEdit(context,view,cancel,ops){
 const s=view.fiveMachineIK,p=s?.pose,h=p?.hudEditing;if(!h)return false;
 if(!cancel&&p.error)throw Error(p.error);
 delete p.hudEditing;
 if(cancel){const error=p.error,{hudValue,...before}=h;Object.assign(p,before);delete p.confirmingHud;if(view.error===error)view.error='';if(s.hud&&hudValue!==undefined)s.hud.value=hudValue;}
 else if(p.dirty)p.confirmingHud=h;
 p.editKey=null;if(p.dirty&&!p.error)schedulePoseEdit(context,view,p,ops,0);renderPose(context,view,ops);return true;
}
export function cancelMachinePoseDraft(context,view,ops){
 const s=view.fiveMachineIK,p=s?.pose;if(!p?.dirty||s.dragging||s.inflight||s.editTask||s.finishing||p.saving)return false;
 clearTimeout(p.timer);p.timer=null;p.requested=null;s.drafts?.delete(p.key);s.pose=null;s.hud=null;renderPose(context,view,ops);return true;
}
export function cancelRejectedMachinePoseDrafts(view){
 const s=view.fiveMachineIK;if(!s||s.dragging||s.inflight||s.editTask||s.finishing)return false;let cancelled=false;
 for(const [key,p] of s.drafts||[])if(p.dirty&&p.error&&!p.saving){
  clearTimeout(p.timer);p.timer=null;p.requested=null;s.drafts.delete(key);cancelled=true;
  if(s.pose===p){s.pose=null;s.hud=null;}else if(s.hud?.tip===p.tip&&s.machineId===p.machineId)s.hud=null;
  if(view.error===p.error)view.error='';
 }
 return cancelled;
}
export function selectMachineAxis(view,axis,direction){
 const s=view.fiveMachineIK,object=view.viewport?.sceneObjects.get(view.fiveMachineSelectedPartId);
 if(!s||!object)return;
 if(!['X','Y','Z','E'].includes(axis)){s.hud=null;return;}
 object.updateWorldMatrix(true,false);const baseWorld=object.matrixWorld.clone(),rotation=new THREE.Quaternion();baseWorld.decompose(new THREE.Vector3(),rotation,new THREE.Vector3());
 const worldAxis=axis==='E'?new THREE.Vector3(...direction):new THREE.Vector3(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis]);
 if(axis!=='E'&&view.fivePlacementSpace==='local')worldAxis.applyQuaternion(rotation);
 s.hud={tip:view.fiveMachineSelectedPartId,mode:s.mode,space:view.fivePlacementSpace||'world',axis,baseWorld,direction:worldAxis.normalize().toArray(),value:'0',acceptedValue:'0'};
}
export function renderMachineIKTransform(view){
 const s=view.fiveMachineIK,m=machine(view);
 if(!s?.mode||view.fiveAxisArea!=='simulation'||activeTool(view)!=='machine-instances'||s.machineId!==m?.entityId||m.enabled===false||!canAdjustMachinePart(view))return '';
 const joint=selectedMachineJoint(view),switcher={label:'运动方式',mode:s.motionMode||'IK',modes:[['FK','FK','five.machine-ik.fk',!joint,joint?'直接调整当前关节':'此部件没有独立运动关节'],['IK','IK','five.machine-ik.ik',false,'拖动目标，由上游关节配合运动']]};
 if(s.motionMode==='FK'){
  const j=selectedMachineJoint(view);if(!j)return '';const busy=!!(view.pending||view.fiveMachine?.busy||view.fiveMachineJog?.running||s.gesture||s.finishing||s.inflight||s.spaceChanging),linear=j.type==='prismatic';
  return renderTransformTools({owner:'machine-joint',title:'关节运动 · FK',mode:s.mode,busy,switcher,modes:[[s.mode,linear?'沿关节轴移动':'绕关节轴旋转','five.machine-ik.fk']]})+renderTransformValue({owner:'machine-joint',mode:s.mode,title:linear?'关节位置 · mm':'关节角度 · °',fields:[{label:j.jointName||j.name,name:`joint:${j.entityId}`,value:rounded(j.position*(linear?1:180/Math.PI)),aria:'浮动关节值',readonly:true}],attribute:'data-five-machine-field',busy,error:s.message||s.fileMessage});
 }
 const p=poseDraft(view);if(!p)return '';const mode=ikMode(view,s.mode),busy=!!(view.pending||view.fiveMachine?.busy||view.fiveMachineJog?.running||s.dragging||s.editTask||s.finishing||s.inflight||s.spaceChanging);
 const modes=[['translate','平移','five.machine-ik.translate']];if(canRotateMachinePart(view))modes.push(['rotate','旋转','five.machine-ik.rotate']);
 const tools=renderTransformTools({owner:'machine-ik',title:'节点目标 · IK',mode,busy,switcher,modes}),h=s.hud;
 if(!h||h.tip!==s.tip||h.mode!==mode||h.space!==(view.fivePlacementSpace||'world'))return tools;
 return tools+renderTransformValue({owner:'machine-ik',mode,axis:h.axis.toLowerCase(),fields:[{label:h.axis==='E'?'视图':h.axis,name:'ik-delta',value:h.value,aria:mode==='translate'?'位移':'角度'}],attribute:'data-five-machine-field',busy,error:p.error||s.message||s.fileMessage});
}
export function renderMachineIK(view){
 const m=machine(view),tip=m?.elements?.find(e=>e.entityId===view.fiveMachineSelectedPartId&&e.kind!=='machine');
 if(!tip)return section('machine-ik','部件位姿',hint('在结构树或三维中选择要调整的部件。'));
 const p=poseDraft(view),s=view.fiveMachineIK,editable=movable(m,tip.entityId),rotatable=canRotateMachinePart(view),busy=view.pending||view.fiveMachine?.busy||view.fiveMachineJog?.running||s.gesture||s.finishing||s.spaceChanging||m.enabled===false,disabled=!editable||busy||s.inflight||s.editTask?'disabled':'',inputDisabled=!editable||view.fiveMachine?.busy||view.fiveMachineJog?.running||s.gesture||s.finishing||m.enabled===false||s.motionMode==='FK'?'disabled':'';
 const fields=p?p.values.map((v,i)=>`<label class="five-machine-field" data-five-key="pose-${attr(tip.entityId)}-${i}"><span>${['X','Y','Z','Rx','Ry','Rz'][i]}</span><input type="text" inputmode="decimal" aria-label="${i<3?'位置':'空间角'} ${['X','Y','Z'][i%3]}" data-five-machine-field="ik-pose:${i}" data-five-key="pose-input-${attr(tip.entityId)}-${i}" value="${attr(p.edited.has(i)?v:rounded(v))}" ${inputDisabled} ${view.pending||s.spaceChanging?'readonly':''}></label>`):[];
 const position=`<div class="five-machine-vector" data-five-key="pose-position-${attr(tip.entityId)}"><strong>位置 · mm</strong>${fields.slice(0,3).join('')}</div>`;
 const rotation=rotatable?`<div class="five-machine-vector" data-five-key="pose-rotation-${attr(tip.entityId)}"><strong>空间角 · °</strong>${fields.slice(3).join('')}</div>`:'';
 const end=s.gesture&&!s.dragging&&!s.inflight&&!s.finishing?button('five.machine-ik.close','结束调整',disabled):'';
 const status=`<div class="five-machine-ik-status" data-machine-ik-status role="status">${esc(p?.error||s.fileMessage||(p?.dirty?'正在调整…':s.tip===tip.entityId?s.message||'':''))}</div>`;
 return section('machine-ik',tip.name||'部件位姿',position+rotation+end+status+(s.fileDirty&&!s.fileSaving?button('five.machine-ik.retry','重试保存',disabled):''),editable?s.motionMode||'FK':'固定');
}
export async function finishMachineIK(view){const s=view?.fiveMachineIK;if(!s)return;if(s.pointer?.waiting&&!s.pointer.ended)throw Error('请先结束部件拖动。');s.dragging=false;for(;;){const current=s.gesture;if(current){current.pointer.ended=true;current.pointer.lifecycle?.abort();await s.completeGesture?.();if(s.gesture===current)return;}while(s.inflight)await s.inflight;await s.finishing;await s.drainPointers?.();await s.promoting;if(!s.gesture&&!s.pointerQueue?.length)break;if(s.gesture&&!s.gesture.pointer.ended)break;if(s.gesture===current)return;}}
export function disposeMachineIK(view){const s=view?.fiveMachineIK;if(!s)return;for(const p of s.pointerQueue||[]){p.cancel=true;p.lifecycle?.abort();}s.pointerQueue=[];if(s.pointer){s.pointer.cancel=true;s.pointer.ended=true;s.pointer.lifecycle?.abort();}s.pointer=null;s.pending=null;s.dragging=false;s.hud=null;if(s.gesture){s.gesture.cancel=true;s.gesture.pointer.ended=true;}if(s.manipulator&&s.viewport?.transformManipulator===s.manipulator)s.viewport.clearTransformManipulator();s.manipulator=null;s.mode=null;if(s.gesture)void finishMachineIK(view);else reader(view)?.setPollInterval?.(100);}
export async function machineIKCommand(context,view,mode,ops){
 if(mode==='close'){await finishMachineIK(view);if(!view.fiveMachineIK?.gesture)disposeMachineIK(view);ops.renderProject(context,view);return;}
 const m=machine(view);if(!m||m.enabled===false)return;
 if(mode==='rotate'&&!canRotateMachinePart(view))return;
 const s=view.fiveMachineIK??={};
 const preferred=view.fiveMachineSelectedPartId;
 s.tip=movable(m,preferred)?preferred:null;
 if(!s.tip)return;
 if(mode==='retry'){await savePoseProject(context,view,ops);return;}
 await flushMachinePoseEdits(context,view,ops);
 s.pose=null;s.hud=null;
 if(mode==='fk'&&!selectedMachineJoint(view))return;
 s.motionMode=mode==='fk'?'FK':'IK';s.preference=s.motionMode;
 s.mode=mode==='fk'?(selectedMachineJoint(view).type==='prismatic'?'translate':'rotate'):ikMode(view,mode==='ik'?s.ikMode:mode);
 if(s.motionMode==='IK')s.ikMode=s.mode;
 view.fiveMachineSelectedPartId=s.tip;s.machineId=m.entityId;s.message='';s.blocked=false;ops.renderProject(context,view);
}
async function applyTarget(context,view,m,s,target){
 const begin=performance.now();
 // Numeric commits are low-frequency, reliable commands. Pointer samples use
 // the separate PDO stream below and never enter this command path.
 await flushMachineColliderSaves(context,view);
 const geometrySignature=machineCollisionFeedbackGeometrySignature(view,m.entityId);
  const r=await context.sceneProxy.invoke('CamSimulation.MoveMachineIK',{expected:poseExpected(view),machineId:m.entityId,tipId:s.tip,target,targetPolicy:'Closest'},{timeoutMs:5000});
 view.fiveMachineIK.lastResult=r;view.fiveMachineIK.lastRoundTripMs=performance.now()-begin;
 setMachineCollisionFeedback(view,m.entityId,r,{geometrySignature});syncMachineCollisionFeedback(view);
 acceptPoseExpected(view,r.expected);
  if(!acceptedPose(r))throw Error(r.reason||'目标不可达或碰撞检查未通过，已停止。');
 for(const j of m.joints||[])if(Object.hasOwn(r.positions,j.entityId)){
  j.position=r.positions[j.entityId];for(const input of context.mount?.querySelectorAll(`[data-five-machine-field="joint:${j.entityId}"]`)||[])if(document.activeElement!==input)input.value=rounded(j.position*(j.type==='prismatic'?1:180/Math.PI));
 }
 view.fiveMachine.report=null;view.fiveMachine.snapshot=null;view.fiveMachine.snapshotKey=null;
  await reader(view)?.poll?.();await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});view.fiveMachineIK.message=poseFeedback(r);return r;
}
export function syncMachineIK(context,view,ops){
 const s=view.fiveMachineIK;if(!s?.mode)return;
 const m=machine(view),vp=view.viewport;
 if(!vp||view.fiveAxisArea!=='simulation'||activeTool(view)!=='machine-instances'||!m||m.entityId!==s.machineId||m.enabled===false){disposeMachineIK(view);return;}
 if(!s.dragging&&!s.inflight&&!s.finishing){if(!movable(m,view.fiveMachineSelectedPartId)){disposeMachineIK(view);return;}s.tip=view.fiveMachineSelectedPartId;}
 const joint=selectedMachineJoint(view),fk=s.motionMode==='FK';if(fk&&!joint){disposeMachineIK(view);return;}
 if(!fk)s.mode=ikMode(view,s.mode);
 const object=vp.sceneObjects.get(s.tip);if(!object)return;
 object.updateWorldMatrix(true,false);const origin=(s.pointer?.waiting&&!s.pointer.cancel?s.pointer.previewWorld:object.matrixWorld).clone(),position=new THREE.Vector3(),rotation=new THREE.Quaternion(),scale=new THREE.Vector3();origin.decompose(position,rotation,scale);
 const message=text=>{s.message=text;const node=context.mount.querySelector('[data-machine-ik-status]');if(node)node.textContent=text;};
 const render=()=>renderPose(context,view,ops);
 const completeGesture=()=>{
  if(s.finishing)return s.finishing;
  const gesture=s.gesture;if(!gesture||!gesture.pointer.ended)return;
  s.finishing=(async()=>{try{
   await gesture.begin;
   if(!gesture.cancel){try{await gesture.stream.flush();}catch(error){gesture.cancel=true;s.blocked=true;message(error.message);}}
   await gesture.stream.stop();
   const receipt=await gesture.pointer.proxy.invoke('CamSimulation.EndMachinePoseEdit',{editToken:gesture.token,cancel:!!gesture.cancel,finalSequence:String(gesture.stream.sequence)});
   if(receipt.pending)throw Error('末次拖动目标尚未处理，调整尚未提交。');
   gesture.cancel=receipt.cancelled===true;
   gesture.ended=true;
   if(gesture.axisHud&&gesture.cancel&&s.pointer===gesture.pointer)s.hud=gesture.hudBefore||null;
   acceptPoseExpected(view,receipt.expected);
   s.lastStreamStats={...gesture.stream.stats,sequence:String(gesture.stream.sequence),writtenSequence:String(gesture.stream.writtenSequence),processedSequence:String(gesture.stream.processedSequence),resultSequence:String(gesture.stream.resultSequence),cancelled:!!gesture.cancel};
   gesture.lifecycle?.abort();
   s.gesture=null;s.pending=null;if(s.pointer===gesture.pointer){s.pose=null;delete s.transformTarget;s.drafts?.clear();}
   // Committed native facts must be persisted even if the following view or
   // history read fails. Presentation cannot erase that save obligation.
   if(!gesture.cancel&&receipt.changed)void savePoseProject(context,view,ops);
   if(gesture.cancel||gesture.blocked)await ops.refreshSceneState(context,view);
   else{
    // Settle a possible pre-gesture resource request, then explicitly wait for
    // the final snapshot to be applied, not merely fetched by its listener.
    await reader(view)?.poll?.();
    if(ops.refreshActiveAreaView)await ops.refreshActiveAreaView(context,view,{});
   }
   await context.actions?.refreshNativeProjectHistory?.({projectId:context.project?.projectId});
  }catch(e){
   message(e.message);
   if(!gesture.beginIssued){gesture.ended=true;s.gesture=null;s.pending=null;}
   if(!gesture.ended){
    // A successful Begin can outlive a timed-out response or a failed PDO
    // initialization. Retire that token reliably before releasing UI guards.
    gesture.cancel=true;
    try{await gesture.stream?.stop();const receipt=await gesture.pointer.proxy.invoke('CamSimulation.EndMachinePoseEdit',{editToken:gesture.token,cancel:true});if(receipt.pending)throw Error('本次调整尚未结束，请点击“结束调整”重试。');acceptPoseExpected(view,receipt.expected);gesture.cancel=receipt.cancelled===true;gesture.ended=true;s.gesture=null;s.pending=null;if(s.pointer===gesture.pointer){s.pose=null;s.drafts?.clear();delete s.transformTarget;}gesture.lifecycle?.abort();
     // A timed-out End may already have committed. Its cached terminal result,
     // rather than this retry's cancellation intent, owns the save obligation.
     if(!gesture.cancel&&receipt.changed)void savePoseProject(context,view,ops);
     await ops.refreshSceneState(context,view);
    }catch(cancelError){message(e.message+'；取消调整失败：'+cancelError.message);}
   }
  }
  finally{const ownsPointer=s.pointer===gesture.pointer;if(gesture.ended&&gesture.cancel&&gesture.axisHud&&ownsPointer)s.hud=gesture.hudBefore||null;if(gesture.ended&&!view.disposed)vp.clearTransformPreviews?.(gesture.descriptor?.worldTransformIds||gesture.descriptor?.worldIds||[]);if(!s.gesture){s.blocked=false;reader(view)?.setPollInterval?.(100);for(const r of gesture.pausedReaders||[])r.setPollingPaused?.(false);}if(gesture.ended&&ownsPointer)s.pointer=null;s.finishing=null;render();if(gesture.ended&&!gesture.cancel&&gesture.focusNumeric&&ownsPointer&&s.hud){const input=context.mount.querySelector('.five-placement-hud [data-five-machine-field="ik-delta"]');input?.focus({preventScroll:true});input?.select();}if(gesture.ended)queueMicrotask(()=>void s.drainPointers?.());}})();return s.finishing;
 };
 const pointerContextCurrent=pointer=>!pointer.cancel&&!view.disposed&&context.isCurrentProject?.()!==false&&activeTool(view)==='machine-instances'&&view.fiveAxisArea==='simulation'&&pointer.proxy===context.sceneProxy&&pointer.viewport===view.viewport&&pointer.machineId===machine(view)?.entityId&&pointer.tip===view.fiveMachineSelectedPartId&&pointer.space===(view.fivePlacementSpace||'world')&&pointer.motionMode===s.motionMode&&pointer.mode===s.mode;
 s.completeGesture=completeGesture;
 const targetFor=pointer=>{
  if(pointer.fk)return Math.max(pointer.lower,Math.min(pointer.upper,pointer.jointStart+pointer.jointDelta));
  const p=new THREE.Vector3(),q=new THREE.Quaternion(),scale=new THREE.Vector3();pointer.safeWorld.decompose(p,q,scale);
  const delta=pointer.translation.clone();if(pointer.space==='local')delta.applyQuaternion(q);p.add(delta);
  const axis=pointer.space==='local'&&pointer.axis!=='E'?pointer.localAxis.clone().applyQuaternion(q):pointer.axisWorld;
  q.premultiply(new THREE.Quaternion().setFromAxisAngle(axis,pointer.rotationAngle)).normalize();return row(new THREE.Matrix4().compose(p,q,new THREE.Vector3(1,1,1)));
 };
 const publishPointer=pointer=>{const gesture=pointer.native;if(!gesture?.stream||gesture.cancel)return;const target=targetFor(pointer);gesture.stream.update(target);s.pending=null;};
 const cancelNative=gesture=>{if(s.gesture!==gesture)return;gesture.cancel=true;gesture.pointer.ended=true;gesture.pointer.lifecycle.abort();if(s.pointer===gesture.pointer){s.dragging=false;s.pending=null;s.manipulator?.finish?.(true);}void completeGesture();};
 const cancelPointer=pointer=>{
  if(!pointer||pointer.ended)return;pointer.cancel=true;pointer.ended=true;pointer.lifecycle.abort();
  if(pointer.native){cancelNative(pointer.native);return;}
  s.pointerQueue=s.pointerQueue.filter(p=>p!==pointer);if(s.pointer===pointer){s.hud=pointer.hudBefore;s.pointer=s.pointerQueue.at(-1)||s.gesture?.pointer||null;s.dragging=false;}render();
 };
 const startGesture=pointer=>{
  const gesture=s.gesture={token:crypto.randomUUID(),lifecycle:pointer.lifecycle,pointer,jointStart:joint?.position,frame:pointer.frame,axisHud:!pointer.fk,hudBefore:pointer.hudBefore};pointer.native=gesture;
  s.dragging=!!s.pointer&&!s.pointer.ended;s.blocked=false;s.pending=null;message('');
  // Inactive areas retain their native View references too. Pause this
  // project's readers together while its accepted PDO matrices are live.
  gesture.pausedReaders=[...new Set(Object.values(view.areas||{}).map(area=>area.viewReader).filter(r=>r&&r.pollingPaused!==true))];
  for(const r of gesture.pausedReaders)r.setPollingPaused?.(true);
  const doc=context.mount.ownerDocument;
  gesture.begin=(async()=>{
   await flushMachineColliderSaves(context,view);
   if(!pointerContextCurrent(pointer))throw Error('部件或编辑上下文已改变，本次待处理拖动已取消。');
   gesture.geometrySignature=machineCollisionFeedbackGeometrySignature(view,m.entityId);
   gesture.beginIssued=true;const descriptor=await context.sceneProxy.invoke('CamSimulation.BeginMachinePoseEdit',{expected:poseExpected(view),machineId:pointer.machineId,tipId:pointer.tip,editToken:gesture.token,motionMode:pointer.motionMode,...(!pointer.fk?{targetPolicy:'Closest'}:{})},{timeoutMs:5000});
   gesture.descriptor=descriptor;acceptPoseExpected(view,descriptor.expected);
   if(!pointerContextCurrent(pointer))throw Error('部件或编辑上下文已改变，本次拖动已取消。');
   if(pointer.wasQueued){
    const authoritative=actualPose(descriptor);if(!authoritative)throw Error('待处理拖动缺少机床实际姿态，本次调整已取消。');pointer.safeWorld=authoritative;
    if(pointer.fk){const index=descriptor.jointIds.indexOf(pointer.tip),value=descriptor.positions?.[index];if(!Number.isFinite(value))throw Error('待处理拖动缺少实际关节位置，本次调整已取消。');pointer.jointStart=value;}
    if(s.pointer===pointer&&s.hud){s.hud.baseWorld=authoritative.clone();const rotation=new THREE.Quaternion();authoritative.decompose(new THREE.Vector3(),rotation,new THREE.Vector3());s.hud.direction=(pointer.space==='local'&&pointer.axis!=='E'?pointer.localAxis.clone().applyQuaternion(rotation):pointer.axisWorld).toArray();}
   }
   pointer.waiting=false;
   gesture.stream=new MachineIKPDOStream(context.sceneProxy.pdo,descriptor,{onResult:r=>{
    if(s.gesture!==gesture||gesture.cancel||view.disposed||context.isCurrentProject?.()===false)return;
    if(!pointerContextCurrent(pointer)){cancelNative(gesture);return;}
    s.lastResult=r;
    if(r.status==='Invalidated'||r.status==='Error'){if(setMachineCollisionFeedback(view,m.entityId,r))queueMicrotask(render);return;}
    if(r.positions?.length!==descriptor.jointIds.length||!descriptor.worldIds?.length||(descriptor.worldTransformIds||descriptor.worldIds).length!==descriptor.worldIds.length||r.worldMatrices?.length!==descriptor.worldIds.length*16)throw Error('实时机床姿态回执不完整，已取消本次调整。');
    acceptPoseExpected(view,{epoch:r.epoch,mutationSequence:r.mutationSequence});
    const positions=new Map(descriptor.jointIds.map((id,i)=>[id,r.positions[i]]));
    for(const j of m.joints||[])if(positions.has(j.entityId)&&Number.isFinite(positions.get(j.entityId))){
     j.position=positions.get(j.entityId);for(const input of context.mount.querySelectorAll(`[data-five-machine-field="joint:${j.entityId}"]`))if(doc.activeElement!==input)input.value=rounded(j.position*(j.type==='prismatic'?1:180/Math.PI));
    }
    if(r.worldMatrices?.length===descriptor.worldIds.length*16){
     const matrices=descriptor.worldIds.map((id,i)=>[(descriptor.worldTransformIds||descriptor.worldIds)[i],new THREE.Matrix4().set(...r.worldMatrices.slice(i*16,i*16+16)).elements]);
     vp.setTransformPreviews(matrices);
    }
    view.fiveMachine.report=null;view.fiveMachine.snapshot=null;view.fiveMachine.snapshotKey=null;s.pose=null;
    const feedbackChanged=setMachineCollisionFeedback(view,m.entityId,r,{geometrySignature:gesture.geometrySignature});syncMachineCollisionFeedback(view);if(feedbackChanged)queueMicrotask(render);
     if(s.pointer===pointer&&!pointer.fk&&r.status!=='Idle'){acceptAxisValue(view,r);for(const input of context.mount.querySelectorAll('.five-placement-hud [data-five-machine-field="ik-delta"]'))if(doc.activeElement!==input)input.value=s.hud?.value??'';}
     if(s.pointer===pointer||!s.pointer)message(r.status==='Idle'?'':acceptedPose(r)?poseFeedback(r):r.reason||'此位置不可达或有碰撞，请继续调整。');
   },onError:error=>{if(s.gesture!==gesture)return;message(error.message);gesture.blocked=true;cancelNative(gesture);}}).start();
   if(!gesture.cancel&&pointer.changed)publishPointer(pointer);
  })();
  // Begin is the sole reliable setup request. Pointer changes while it is in
  // flight replace s.pending, and pointerup waits for this same promise.
  const inflight=gesture.begin.catch(error=>{message(error.message);gesture.blocked=true;gesture.cancel=true;pointer.ended=true;if(s.pointer===pointer)s.dragging=false;void completeGesture();}).finally(()=>{if(s.inflight===inflight)s.inflight=null;});s.inflight=inflight;
  context.actions?.refreshProjectHistoryControls?.();
  return gesture;
 };
 s.pointerQueue??=[];
 s.drainPointers=()=>{
  if(s.promoting)return s.promoting;if(s.gesture||s.finishing||s.inflight||!s.pointerQueue.length)return;
  const promoting=(async()=>{await Promise.resolve();try{
   while(!s.gesture&&!s.finishing&&s.pointerQueue.length){const pointer=s.pointerQueue.shift();if(pointer.cancel)continue;const gesture=startGesture(pointer);try{await gesture.begin;}catch{await s.inflight;}if(pointer.ended)await completeGesture();else break;if(s.gesture)break;}
  }finally{if(s.promoting===promoting)s.promoting=null;render();}})();s.promoting=promoting;return promoting;
 };
 s.viewport=vp;
 const axis=fk?new THREE.Vector3(...joint.axis).normalize():null,worldAxis=axis?.clone().applyQuaternion(rotation),handleRotation=fk?rotation.clone().multiply(new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0,0,1),axis)):rotation;
 vp.setTransformManipulator({mode:s.mode,space:fk?'local':view.fivePlacementSpace||'world',axes:fk?['Z']:undefined,position:position.toArray(),quaternion:handleRotation.toArray(),planeTranslate:!fk,freeTranslate:!fk,screenRotate:!fk,enabled:!view.pending&&!view.fiveHistoryBusy&&!view.fiveMachine?.busy&&!view.fiveMachineJog?.running&&!s.pose?.dirty&&!s.editTask&&!s.spaceChanging&&(!s.gesture||s.gesture.pointer.ended||s.dragging),
  onPosition:p=>positionTransformHud(context,p,fk?'machine-joint':'machine-ik'),
  onStart:({axis,direction}={})=>{if(view.pending||view.fiveHistoryBusy||view.disposed||context.isCurrentProject?.()===false||view.viewport!==vp||machine(view)?.entityId!==m.entityId||s.tip!==view.fiveMachineSelectedPartId||s.pose?.dirty||s.editTask||s.spaceChanging||s.pointer&&!s.pointer.ended)return false;const before=s.hud;if(!fk)selectMachineAxis(view,axis,direction);
   const proxy=vp.transformManipulator.proxy;proxy.updateMatrixWorld(true);const downWorld=proxy.matrixWorld.clone(),downRotation=new THREE.Quaternion();downWorld.decompose(new THREE.Vector3(),downRotation,new THREE.Vector3());
   const axisWorld=fk?worldAxis.clone():axis==='E'?new THREE.Vector3(...direction):new THREE.Vector3(...({X:[1,0,0],Y:[0,1,0],Z:[0,0,1]})[axis]||[0,0,1]);if(!fk&&axis!=='E'&&view.fivePlacementSpace==='local')axisWorld.applyQuaternion(downRotation);axisWorld.normalize();
   const pointer={machineId:m.entityId,tip:s.tip,motionMode:s.motionMode,fk,mode:s.mode,axis,space:view.fivePlacementSpace||'world',proxy:context.sceneProxy,viewport:vp,frame:poseDraft(view)?.frame,hudBefore:before,lifecycle:new AbortController(),downWorld,downRotation,safeWorld:object.matrixWorld.clone(),previewWorld:downWorld.clone(),axisWorld,localAxis:axisWorld.clone().applyQuaternion(downRotation.clone().invert()),translation:new THREE.Vector3(),rotationAngle:0,jointDelta:0,jointStart:joint?.position,lower:joint?.lower,upper:joint?.upper,ended:false,wasQueued:!!(s.gesture||s.finishing||s.inflight||s.promoting||s.pointerQueue.length)};
   s.pointer=pointer;s.dragging=true;const doc=context.mount.ownerDocument;doc.defaultView?.addEventListener?.('blur',()=>cancelPointer(pointer),{signal:pointer.lifecycle.signal});doc.addEventListener?.('visibilitychange',()=>{if(doc.hidden)cancelPointer(pointer);},{signal:pointer.lifecycle.signal});
   if(pointer.wasQueued){pointer.waiting=true;s.pointerQueue.push(pointer);message('正在完成上一项调整，当前拖动已保留。');void s.drainPointers();}else startGesture(pointer);queueMicrotask(render);},
  onChange:({translation,angle})=>{const pointer=s.pointer;if(!pointer||pointer.cancel||pointer.ended||!s.dragging)return;
   const proxy=vp.transformManipulator.proxy;proxy.updateMatrixWorld(true);pointer.previewWorld=proxy.matrixWorld.clone();pointer.changed=true;
   if(fk){
    const delta=joint.type==='prismatic'?new THREE.Vector3(...translation).dot(pointer.axisWorld):angle;
    pointer.jointDelta=delta;const target=Math.max(joint.lower,Math.min(joint.upper,pointer.jointStart+delta));
    const offset=target-pointer.jointStart;
    if(joint.type==='prismatic')proxy.position.setFromMatrixPosition(pointer.downWorld).addScaledVector(pointer.axisWorld,offset);
    else proxy.quaternion.copy(pointer.downRotation).multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,0,1),offset));
    proxy.updateMatrixWorld(true);pointer.previewWorld=proxy.matrixWorld.clone();publishPointer(pointer);return;
   }
   const matrix=proxy.matrixWorld;pointer.translation.copy(new THREE.Vector3().setFromMatrixPosition(matrix).sub(new THREE.Vector3().setFromMatrixPosition(pointer.downWorld)));if(pointer.space==='local')pointer.translation.applyQuaternion(pointer.downRotation.clone().invert());pointer.rotationAngle=pointer.mode==='rotate'?angle:0;
   const h=s.hud;if(h){const value=s.mode==='translate'?new THREE.Vector3().setFromMatrixPosition(matrix).sub(new THREE.Vector3().setFromMatrixPosition(h.baseWorld)).dot(new THREE.Vector3(...h.direction)):angle*180/Math.PI;h.value=rounded(value);for(const input of context.mount.querySelectorAll('.five-placement-hud [data-five-machine-field="ik-delta"]'))if(input!==context.mount.ownerDocument.activeElement)input.value=h.value;}
   publishPointer(pointer);},
  onEnd:({moved}={})=>{const pointer=s.pointer;if(!pointer)return;pointer.ended=true;pointer.lifecycle.abort();if(pointer.native)pointer.native.focusNumeric=!moved;s.dragging=false;if(pointer.native)void completeGesture();else void s.drainPointers();},
  onCancel:()=>cancelPointer(s.pointer)
 });s.manipulator=vp.transformManipulator;
}
