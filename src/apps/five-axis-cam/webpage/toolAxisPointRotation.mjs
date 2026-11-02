import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { toolAxisPointCommand, syncToolAxisPoints } from './toolAxisPoints.mjs';
import { escapeAttr as attr, escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { syncToolAxisVectorFields, validateToolAxisAngles } from './toolAxisAngles.mjs';
import {renderTransformTools,positionTransformHud,positionWorldTransformHud} from './transformHud.mjs';

const inputSelector='[data-five-point-angle]';
const alive=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const format=n=>Math.abs(n)<1e-10?'0':Number(n.toFixed(8)).toString();
const unit=values=>{const v=values.map(n=>String(n).trim()?Number(n):NaN),length=Math.hypot(...v);if(!v.every(Number.isFinite)||length<1e-12)throw Error('请输入有效的 IJK，且不能为零向量。');return new THREE.Vector3(...v).normalize();};
const selected=view=>{const ui=workspaceState(view),ids=ui.selectedIntentIds||[ui.selectedIntentId];return ids.length===1?view.scene?.intentToolpaths.find(c=>c.id===ids[0]):null;};
const placement=(view,cut)=>view.scene?.domain?.placements.find(p=>p.id===cut.placementId);
const rotation=p=>new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().set(...p.transform));
const row=d=>[...(d.points||[]),...(d.newPoint?[d.newPoint]:[])].find(p=>p.id===d.pointId);
function restoreGesture(d){
  const r=d?.rotation;if(!r?.gesture)return;
  const saved=r.gesture;Object.assign(d,{values:saved.values,frame:saved.frame,dirty:saved.dirty,error:saved.error,rotation:saved.rotation});
}
function placeHud(context,point){
  positionTransformHud(context,point,'point');
}
function updateValues(context,view,ops){
  const d=view.fiveAxisPointEdit;
  syncToolAxisVectorFields(context.mount,view,d,'point');
  const angle=context.mount.querySelector(inputSelector);if(angle&&angle.value!==d.rotation.value)angle.value=d.rotation.value;
  syncToolAxisPoints(context,view,ops);
}
function preview(view){
  const d=view.fiveAxisPointEdit,r=d?.rotation,cut=selected(view),p=cut&&placement(view,cut);
  if(!r||!p||r.pointId!==d.pointId||r.key!==d.key)throw Error('关键点已改变，请重新选择旋转环。');
  if(!String(r.value).trim()||!Number.isFinite(Number(r.value)))throw Error('请输入有效旋转角度。');
  const q=rotation(p),axis=new THREE.Vector3(...(r.axis==='e'?r.rotationAxis:{x:[1,0,0],y:[0,1,0],z:[0,0,1]}[r.axis]));
  if(r.axis!=='e'&&r.space==='local')axis.applyQuaternion(q);
  const v=unit(r.baseValues);if(r.baseFrame==='Workpiece')v.applyQuaternion(q);
  v.applyAxisAngle(axis,(Number(r.value)%360)*Math.PI/180);
  if(d.frame==='Workpiece')v.applyQuaternion(q.invert());
  d.values=v.normalize().toArray().map(format);d.dirty=r.savedDirty||d.values.some((n,i)=>Math.abs(Number(n)-Number(r.savedValues[i]))>1e-9);d.error='';
}
export function renderToolAxisPointRotation(view){
  const d=view.fiveAxisPointEdit,r=d?.rotation;
  const cut=selected(view);if(view.fiveAxisArea!=='toolpath'||view.fivePoseSelection?.active||activeTool(view)!=='keypoint'||!d?.axis||!cut||cut.id!==d.id||cut.locked||d.loading||d.stale||d.picking||d.influence?.picking||d.influenceLoading)return '';
  const selecting=view.fiveAxisPointInsert||view.fiveAxisPointDelete,tools=renderTransformTools({owner:'point',title:'刀轴变换',mode:selecting?'':'rotate',busy:view.pending||d.request,modes:[['rotate','旋转','five.points.edit']]});
  if(selecting||!r||r.pointId!==d.pointId)return tools;
  return tools+`<div class="five-placement-hud five-point-angle-hud" data-transform-owner="point" data-five-key="point-angle-hud" data-axis="${r.axis}" role="group" aria-label="刀轴旋转"><label class="five-placement-field" data-axis="${r.axis}"><span>${r.axis==='e'?'视图':r.axis.toUpperCase()}</span><input type="text" inputmode="decimal" autocomplete="off" aria-label="刀轴旋转角度" data-five-point-angle data-five-key="point-angle-value" value="${attr(r.value)}" ${view.pending||d.request?'readonly':''}><small>°</small></label>${d.error?`<div class="five-placement-error" role="alert">${esc(d.error)}</div>`:''}</div>`;
}
export function toolAxisPointAngleChanged(context,view,value,ops){
  const d=view.fiveAxisPointEdit;if(view.fivePoseSelection?.active||!d?.rotation||d.rotation.gesture||d.request||view.pending||selected(view)?.locked)return;
  d.rotation.value=value;try{preview(view);updateValues(context,view,ops);}catch(error){d.error=error.message;}
  ops.renderProject(context,view);
}
export async function toolAxisPointRotationCommand(context,view,action,ops){
  const d=view.fiveAxisPointEdit,r=d?.rotation;if(!r||view.pending||!alive(context,view)||selected(view)?.id!==d.id||selected(view)?.locked||d.loading||d.stale)return;
  if(action==='cancel'){
    if(d.request)return;
    if(r.gesture){view.viewport?.transformManipulator?.finish(true);return;}
    d.values=[...r.savedValues];d.dirty=r.savedDirty;d.frame=r.savedFrame;r.value=r.savedValue;d.error='';
    ops.renderProject(context,view);return;
  }
  if(action!=='apply'||d.request)return;
  try{preview(view);}catch(error){d.error=error.message;ops.renderProject(context,view);return;}
  await toolAxisPointCommand(context,view,'apply',ops);
  if(alive(context,view))ops.renderProject(context,view);
}
export function toolAxisPointSpaceChanged(view,space){
  const d=view.fiveAxisPointEdit;
  if(view.pending||d?.request){view.viewport?.setTransformSpace(view.fivePointRotationSpace||'world');return;}
  restoreGesture(d);view.fivePointRotationSpace=space;if(d)d.rotation=null;
}
export function disposeToolAxisPointRotation(view){
  const p=view.fivePointManipulator;if(!p)return;
  restoreGesture(p.draft);
  if(p.viewport.transformManipulator===p.manipulator)p.viewport.clearTransformManipulator();
  view.fivePointManipulator=null;
}
export function syncToolAxisPointRotation(context,view,ops){
  const d=view.fiveAxisPointEdit,cut=selected(view),vp=view.viewport;
  if(activeTool(view)==='keypoint'&&(view.fiveAxisPointInsert||view.fiveAxisPointDelete))positionWorldTransformHud(context,vp,d&&row(d)?.setupPoint,'point');
  if(view.fivePoseSelection?.active||!alive(context,view)||view.fiveAxisArea!=='toolpath'||activeTool(view)!=='keypoint'||!d?.axis||!cut||cut.id!==d.id||cut.locked||d.loading||d.stale||d.picking||d.influence?.picking||d.influenceLoading||view.fiveAxisPointInsert||view.fiveAxisPointDelete){disposeToolAxisPointRotation(view);return;}
  const p=placement(view,cut),point=row(d);if(!vp||!p||!point){disposeToolAxisPointRotation(view);return;}
  const previous=view.fivePointManipulator;
  if(previous&&(previous.viewport!==vp||previous.draft!==d||previous.pointId!==d.pointId||previous.key!==d.key))disposeToolAxisPointRotation(view);
  if(d.rotation&&(d.rotation.pointId!==d.pointId||d.rotation.key!==d.key)&&!d.request)d.rotation=null;
  let valid=true;try{unit(d.values);validateToolAxisAngles(d);}catch{valid=false;}
  const space=view.fivePointRotationSpace??vp.transformSpace??'world';view.fivePointRotationSpace=space;
  if(vp.transformSpace!==space)vp.setTransformSpace(space);
  // Left-button handle drags own their gesture; middle-button navigation and
  // wheel zoom remain available between pose gestures.
  vp.setCameraNavigationEnabled(true);
  const position=d.positionValues?.every(x=>String(x).trim()&&Number.isFinite(Number(x)))?new THREE.Vector3(...d.positionValues.map(Number)):new THREE.Vector3(...point.setupPoint);
  if(d.positionValues&&d.frame==='Workpiece')position.applyMatrix4(new THREE.Matrix4().set(...p.transform));
  vp.setTransformManipulator({mode:'rotate',space,screenRotate:true,activeAxis:d.rotation?.axis,position:position.toArray(),quaternion:rotation(p).toArray(),enabled:valid&&!view.pending&&!d.request,
    onPosition:point=>placeHud(context,point),
    onStart:({axis,direction})=>{
      if(!alive(context,view)||view.fiveAxisPointEdit!==d||d.request||view.pending)return;
      const gesture={values:[...d.values],frame:d.frame,dirty:d.dirty,error:d.error,rotation:d.rotation?structuredClone(d.rotation):null};
      d.rotation={axis:axis.toLowerCase(),rotationAxis:direction,value:'0',key:d.key,pointId:d.pointId,space,baseValues:[...d.values],baseFrame:d.frame,savedValues:[...d.values],savedFrame:d.frame,savedDirty:d.dirty,savedValue:'0',gesture};
      d.error='';ops.renderProject(context,view);
    },
    onChange:({angle})=>{
      const r=d.rotation;if(!r?.gesture||view.fiveAxisPointEdit!==d||r.key!==d.key||r.pointId!==d.pointId)return;
      r.value=format(angle*180/Math.PI);try{preview(view);updateValues(context,view,ops);}catch(error){d.error=error.message;}
    },
    onEnd:({moved})=>{
      if(!d.rotation?.gesture||view.fiveAxisPointEdit!==d)return;delete d.rotation.gesture;
      if(moved)void toolAxisPointRotationCommand(context,view,'apply',ops);
      else{ops.renderProject(context,view);const input=context.mount.querySelector(inputSelector);input?.focus({preventScroll:true});input?.select();}
    },
    onCancel:()=>{restoreGesture(d);if(alive(context,view))ops.renderProject(context,view);},
  });
  view.fivePointManipulator={draft:d,pointId:d.pointId,key:d.key,viewport:vp,manipulator:vp.transformManipulator};
}
