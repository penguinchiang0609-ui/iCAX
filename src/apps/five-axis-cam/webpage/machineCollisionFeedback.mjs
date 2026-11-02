import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {addColliderHighlight} from './machineColliderVisuals.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {registerWorkspaceAlertProvider} from './alerts.mjs';

const machineOf=(view,id)=>view.scene?.machines?.find(m=>m.entityId===id);
const attachments=(machine,owner)=>machine.collisions?.filter(c=>c.entityId===owner)||[];
const tuple=value=>Array.isArray(value)&&value.length===3&&value.every(Number.isFinite);
const text=value=>typeof value==='string'?value:'';
const key=ref=>`${encodeURIComponent(ref.ownerElementId)}:${ref.boxIndex}:${encodeURIComponent(ref.boxId)}`;
const epoch=view=>view.scene?.domain?.expected?.epoch??'';
function boundCollisionInputs(view,machineId){
 const domain=view.scene?.domain,setups=(domain?.setups||[]).filter(setup=>setup.machineSetup?.machineId===machineId),setupIds=new Set(setups.map(setup=>setup.id));
 const placements=(domain?.placements||[]).filter(placement=>setupIds.has(placement.setupId)),workpieceIds=new Set(placements.map(placement=>placement.workpieceId));
 const geometry=record=>[record.geometryRef,record.sourceGeometry,record.geometry,record.geometryVersion,record.resourceVersion];
 return [setups.map(setup=>[setup.id,setup.machineSetup]),
  placements.map(placement=>[placement.id,placement.setupId,placement.workpieceId,placement.enabled,placement.collisionEnabled,placement.transform,...geometry(placement)]),
  (domain?.workpieces||[]).filter(workpiece=>workpieceIds.has(workpiece.id)).map(workpiece=>[workpiece.id,workpiece.enabled,workpiece.collisionEnabled,...geometry(workpiece)]),
  (domain?.fixtures||[]).filter(fixture=>setupIds.has(fixture.setupId)).map(fixture=>[fixture.id,fixture.setupId,fixture.unitId,fixture.enabled,fixture.collisionEnabled,fixture.transform,...geometry(fixture)]),
  (domain?.fixtureUnits||[]).filter(unit=>setupIds.has(unit.setupId)).map(unit=>[unit.id,unit.setupId,unit.enabled,unit.partIds,unit.transform,...geometry(unit)])];
}

// These signatures describe scene facts, not selection, camera or edit mode.
// A box can therefore be opened without retiring the diagnosis that led to it.
export function machineCollisionFeedbackGeometrySignature(view,machineId){
 const machine=machineOf(view,machineId);if(!machine)return null;
 const drafts=[...(view.fiveMachineColliders?.drafts.entries()||[])].filter(([id,d])=>d.dirty&&machine.elements?.some(e=>e.entityId===id)).map(([id,d])=>[id,d.boxes]);
 return JSON.stringify([machine.enabled,machine.elements?.map(e=>[e.entityId,e.parentEntityId,e.restPosition,e.restRotation]),machine.joints?.map(j=>[j.entityId,j.type,j.axis,j.lower,j.upper,j.restPosition,j.restRotation]),machine.collisions?.map(c=>[c.entityId,c.id,c.name,c.collisionName,c.enabled,c.pose,c.geometry]),drafts,boundCollisionInputs(view,machineId)]);
}
export function machineCollisionFeedbackSignature(view,machineId){
 const machine=machineOf(view,machineId);if(!machine)return null;
 const object=view.viewport?.sceneObjects.get(machineId);object?.updateWorldMatrix(true,false);
 const root=object?.matrixWorld.elements||view.viewport?.transformPayloads?.get(machineId)?.localToWorld;
 return JSON.stringify([machineCollisionFeedbackGeometrySignature(view,machineId),machine.joints?.map(j=>[j.entityId,j.position]),root]);
}
function disposeOverlays(state){
 for(const {object} of state?.overlays?.values()||[]){object.removeFromParent();object.traverse(o=>{o.geometry?.dispose();for(const material of Array.isArray(o.material)?o.material:[o.material])material?.dispose();});}
 state?.overlays?.clear();
}
export function clearMachineCollisionFeedback(view){
 const state=view.fiveMachineCollisionFeedback;if(!state)return false;disposeOverlays(state);delete view.fiveMachineCollisionFeedback;return true;
}
export const disposeMachineCollisionFeedback=clearMachineCollisionFeedback;
export function invalidateMachineCollisionFeedback(view){
 const state=view.fiveMachineCollisionFeedback;if(!state||state.validity!=='Current')return false;state.validity='Stale';disposeOverlays(state);return true;
}
function validateCurrent(view){
 const state=view.fiveMachineCollisionFeedback;if(!state)return null;
 const currentEpoch=epoch(view),sceneReady=Array.isArray(view.scene?.machines);
 if(state.validity==='Current'&&(view.disposed||currentEpoch&&currentEpoch!==state.epoch||sceneReady&&machineCollisionFeedbackSignature(view,state.machineId)!==state.signature))invalidateMachineCollisionFeedback(view);
 return state;
}
function normaliseEndpoint(value){
 if(!value||!text(value.ownerElementId))return null;
 const boxIndex=Number(value.boxIndex);if(!Number.isSafeInteger(boxIndex)||boxIndex< -1)return null;
 return {ownerElementId:text(value.ownerElementId),ownerElementName:text(value.ownerElementName),boxId:text(value.boxId),boxName:text(value.boxName),boxIndex,representation:text(value.representation),editable:value.editable===true&&boxIndex>=0};
}
export function setMachineCollisionFeedback(view,machineId,result,options={}){
 if(result?.status==='Idle')return false;
 if(!result||result.collisionStatus!=='Blocked'||['Unchecked','Error','Invalidated'].includes(result.status))return clearMachineCollisionFeedback(view);
 if(!machineOf(view,machineId))return Array.isArray(view.scene?.machines)?clearMachineCollisionFeedback(view):false;
 const refs=[],seen=new Set(),pairs=[];let complete=result.collisionDiagnosticsComplete===true;
 for(const pair of Array.isArray(result.collisionPairs)?result.collisionPairs:[]){
  const first=normaliseEndpoint(pair?.first),second=normaliseEndpoint(pair?.second);if(!first||!second){complete=false;continue;}
  pairs.push({first,second});for(const ref of [first,second])if(!seen.has(key(ref))){seen.add(key(ref));refs.push(ref);}
 }
 const summary=JSON.stringify([machineId,result.reason||'',complete,pairs]),signature=machineCollisionFeedbackSignature(view,machineId),previous=validateCurrent(view);
 const validity=options.geometrySignature!=null&&options.geometrySignature!==machineCollisionFeedbackGeometrySignature(view,machineId)?'Stale':'Current';
 if(previous?.summary===summary&&previous.signature===signature&&previous.validity===validity)return false;
 disposeOverlays(previous);
 view.fiveMachineCollisionFeedback={machineId,epoch:epoch(view),signature,geometrySignature:machineCollisionFeedbackGeometrySignature(view,machineId),summary,reason:text(result.reason),complete,refs,pairs,validity,overlays:new Map()};
 return true;
}
function resolveRef(view,state,ref){
 const machine=machineOf(view,state.machineId);if(!machine||!ref.editable)return null;
 const own=attachments(machine,ref.ownerElementId);let index=ref.boxId?own.findIndex(c=>c.id===ref.boxId):-1;
 if(index<0){index=ref.boxIndex;const candidate=own[index];if(!candidate||ref.boxId&&candidate.id&&candidate.id!==ref.boxId)return null;}
 const box=own[index],boxName=box?.name||box?.collisionName||`碰撞盒 ${index+1}`;if(box?.geometry?.type!=='box'||box.enabled===false||ref.boxName&&ref.boxName!==boxName)return null;
 if(!Array.isArray(box.pose)||box.pose.length!==6||!box.pose.every(Number.isFinite)||!tuple(box.geometry.size)||box.geometry.size.some(n=>n<=0))return null;
 const owner=machine.elements?.find(e=>e.entityId===ref.ownerElementId);
 return {...ref,machineId:state.machineId,ownerElementName:ref.ownerElementName||owner?.name||ref.ownerElementId,boxName:ref.boxName||boxName,boxId:box.id||ref.boxId,boxIndex:index,draftIndex:own.slice(0,index).filter(c=>c.geometry?.type==='box').length,attachment:box};
}
export function machineCollisionFeedbackTarget(view,index){
 const state=validateCurrent(view);if(state?.validity!=='Current'||!Number.isSafeInteger(Number(index)))return null;
 const ref=state.refs[Number(index)];if(!ref)return null;const target=resolveRef(view,state,ref);if(!target)return null;
 const {attachment,...result}=target;return result;
}
export function machineCollisionBoxMarked(view,ownerElementId,boxIndexOrId,boxId=''){
 const state=validateCurrent(view);if(state?.validity!=='Current')return false;
 return state.refs.some(ref=>{if(ref.ownerElementId!==ownerElementId)return false;const target=resolveRef(view,state,ref);return !!target&&(typeof boxIndexOrId==='number'&&target.boxIndex===boxIndexOrId||typeof boxIndexOrId==='string'&&!!boxIndexOrId&&target.boxId===boxIndexOrId||!!boxId&&target.boxId===boxId);});
}
function makeOverlay(ref){
 const box=new THREE.BoxGeometry(1,1,1),geometry=new THREE.EdgesGeometry(box);box.dispose();
 const object=new THREE.LineSegments(geometry,new THREE.LineBasicMaterial({color:0xff4444,depthTest:false,depthWrite:false,toneMapped:false}));
 object.name=`Collision collider ${ref.boxName}`;object.renderOrder=1200;object.frustumCulled=false;object.raycast=()=>{};
 object.userData={independentInteractionVisual:true,machineCollisionFeedback:true,ownerElementId:ref.ownerElementId,boxId:ref.boxId,boxIndex:ref.boxIndex};
 const highlight=addColliderHighlight(object,{name:'Collision box highlight',color:0xff4444,borderColor:0x481717,fillOpacity:0,renderOrder:1200,borderWidth:2,lineWidth:2});highlight.visible=true;
 return object;
}
export function syncMachineCollisionFeedback(view){
 const previous=view.fiveMachineCollisionFeedback?.validity,state=validateCurrent(view);if(!state)return false;
 if(state.validity!=='Current'){disposeOverlays(state);return previous!==state.validity;}
 const wanted=new Set(),visible=view.fiveAxisArea==='simulation'&&(!view.fiveMachine?.machineId||view.fiveMachine.machineId===state.machineId);
 for(const ref of state.refs){const resolved=resolveRef(view,state,ref);if(!resolved)continue;const parent=view.viewport?.sceneObjects.get(ref.ownerElementId);if(!parent)continue;
  const identity=key(ref);wanted.add(identity);let overlay=state.overlays.get(identity);
  if(overlay&&overlay.parent!==parent){overlay.object.removeFromParent();overlay.object.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});state.overlays.delete(identity);overlay=null;}
  if(!overlay){overlay={parent,object:makeOverlay(resolved)};parent.add(overlay.object);state.overlays.set(identity,overlay);}
  const pose=resolved.attachment.pose,size=resolved.attachment.geometry.size;overlay.object.visible=visible;overlay.object.position.set(...pose.slice(0,3).map(n=>n*1000));overlay.object.rotation.set(pose[3],pose[4],pose[5],'ZYX');overlay.object.scale.set(...size.map(n=>n*1000));
 }
 for(const [identity,overlay] of state.overlays)if(!wanted.has(identity)){overlay.object.removeFromParent();overlay.object.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});state.overlays.delete(identity);}
 return previous!==state.validity;
}
export function renderMachineCollisionFeedback(view){
 const state=validateCurrent(view);if(!state||view.fiveMachine?.machineId&&view.fiveMachine.machineId!==state.machineId)return '';
 const stale=state.validity!=='Current',items=state.refs.map((ref,index)=>{
  const domain=view.scene?.domain,placement=domain?.placements?.find(p=>p.id===ref.ownerElementId),workpiece=placement&&domain.workpieces?.find(w=>w.id===placement.workpieceId),fixture=domain?.fixtures?.find(f=>f.id===ref.ownerElementId);
  const target=!stale&&machineCollisionFeedbackTarget(view,index),owner=workpiece?.name||fixture?.name||ref.ownerElementName||machineOf(view,state.machineId)?.elements?.find(e=>e.entityId===ref.ownerElementId)?.name||'未命名对象';
  const name=ref.representation==='OwnMesh'?(workpiece?'工件真实网格':fixture?'夹具真实网格':ref.boxName||'真实模型'):ref.boxName||`碰撞盒 ${ref.boxIndex+1}`,label=owner===name?owner:`${owner} · ${name}`;
  return `<div class="five-machine-collision-item" data-five-key="collision-ref-${attr(key(ref))}">${ref.editable?`<button type="button" data-five-command="five.machine.collision-edit:${index}" data-five-key="collision-edit-${attr(key(ref))}" aria-label="编辑碰撞盒 ${attr(label)}" title="选择并编辑此碰撞盒" ${target?'':'disabled'}><span>${esc(label)}</span><small>编辑</small></button>`:`<span>${esc(label)}</span>`}</div>`;
 }).join('');
 const recheck=stale||!state.complete?`<button type="button" class="five-machine-collision-recheck" data-five-command="five.machine.collision-recheck" data-five-key="machine-collision-recheck" aria-label="重新检查机床碰撞" ${view.pending||view.fiveMachine?.busy?'disabled':''}>重新检查</button>`:'';
 return `<section class="five-machine-collision-feedback" data-five-key="machine-collision-feedback" data-machine-collision-feedback data-stale="${stale}" role="status"><strong>${stale?'碰撞定位已过期':'相关碰撞对象'}</strong>${stale?'<p>姿态或参数已修改，请重新检查。</p>':''}<div class="five-machine-collision-pairs">${items||'<p>尚未定位到具体碰撞盒。</p>'}</div>${!stale&&!state.complete?'<small>部分碰撞盒尚未完成定位。</small>':''}${recheck}</section>`;
}

registerWorkspaceAlertProvider('machine-collision-boxes',view=>{
 const state=validateCurrent(view);if(!state)return [];
 if(state.validity!=='Current')return [{id:'stale',title:'机床碰撞定位已过期',detail:'姿态或碰撞盒已变化，点击重新检查',severity:'warning',target:{commandId:'five.machine.collision-recheck'}}];
 return state.refs.flatMap((ref,index)=>{const target=machineCollisionFeedbackTarget(view,index);return target?[{id:key(ref),title:`碰撞盒：${target.ownerElementName} · ${target.boxName}`,detail:'点击定位并编辑此碰撞盒',severity:'error',target:{commandId:`five.machine.collision-edit:${index}`}}]:[];});
});
