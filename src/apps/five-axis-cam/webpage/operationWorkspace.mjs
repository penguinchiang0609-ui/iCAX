import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {button,section,hint,facts} from './workspaceControls.mjs';
import {activeTool,preparationKind,selectTool} from './workspaceModel.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {workpieceSelectionBusy,resetWorkpieceTools,selectWorkpiece} from './workpieceSelection.mjs';
import {activeWorkpiece} from './workpieceState.mjs';
import {renderSetupRow} from './setupManagement.mjs';
import {renderFixtureList} from './fixtureViews.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';

export const machineTypeLabels=Object.freeze({HeadHead:'双摆头',HeadTable:'4+1 · 单摆头＋单转台',TableTable:'3+2 · 固定头＋双转台'});
const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const active=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const queryKey=view=>JSON.stringify([setupOf(view)?.id,view.scene?.domain?.expected]);
export function operationPlacements(view,{enabledOnly=false,workpieceId}={}){
 const d=view.scene?.domain;if(!d)return [];
 return (d.setups||[]).flatMap(s=>{const p=(d.placements||[]).find(p=>p.setupId===s.id);if(!p||workpieceId&&p.workpieceId!==workpieceId||enabledOnly&&(s.enabled===false||!d.workpieces?.some(w=>w.id===p.workpieceId&&w.enabled!==false)))return [];return [p];});
}
export function syncOperationPreparationKind(view){
 if(view.fiveAxisArea==='workpiece')view.fivePreparationKind='workpiece';
 else if(view.fiveAxisArea==='operation'){
  if(!selectedPlacement(view)?.setupId)view.fivePreparationKind='workpiece';
  else if(preparationKind(view)==='workpiece')view.fivePreparationKind='setup';
 }
}
export async function prepareOperationArea(context,view,ops,area){
 if(!active(context,view))return true;
 const d=view.scene?.domain;if(!d)return true;
 const current=selectedPlacement(view)||d.placements.find(p=>p.id===d.activePlacementId);if(!current)return true;
 if(current.setupId){view.fiveOperationPlacementId=current.id;(view.fiveCamPlacements??=new Map()).set(current.workpieceId,current.id);}
 const source=d.placements.find(p=>p.workpieceId===current.workpieceId&&!p.setupId);
 let target;
 if(area==='workpiece'){
  target=source;
 }else if(['operation','toolpath','simulation'].includes(area)){
  const eligible=operationPlacements(view,{enabledOnly:area!=='operation',workpieceId:current.workpieceId});
  target=eligible.find(p=>p.id===current.id)||eligible.find(p=>p.id===view.fiveCamPlacements?.get(current.workpieceId))||eligible.find(p=>p.id===view.fiveOperationPlacementId)||eligible[0]||source;
  if(area!=='operation'&&d.workpieces.find(w=>w.id===current.workpieceId)?.enabled===false)return true;
 }
 if(!target||target.id===current?.id)return true;
 if(workpieceSelectionBusy(view))return false;
 // Page navigation restores the camera for its source/setup frame. A queued
 // return to the still-active page must not refit that saved camera.
 await selectWorkpiece(context,view,target.id,ops,{kind:target.setupId?'setup':'workpiece',visibilityArea:area,fitView:false});
 return active(context,view)&&view.scene.domain.activePlacementId===target.id&&!view.error;
}
function machineState(view){
 const setupId=setupOf(view)?.id;let s=view.fiveMachineTypeAssignments;
 if(!s||s.setupId!==setupId)s=view.fiveMachineTypeAssignments={setupId,serial:(s?.serial||0)+1};
 return s;
}
const control=(name,label,value,values,disabled=false)=>`<div class="five-field" data-five-key="operation-field-${name}"><label for="five-operation-${name}">${esc(label)}</label><select id="five-operation-${name}" data-five-operation-field="${name}" data-five-key="operation-input-${name}" ${name==='workpieceId'?'data-model-value-authoritative="true"':''} aria-label="${esc(label)}" ${disabled?'disabled':''}>${values.map(([id,text,off])=>`<option value="${attr(id)}" ${id===value?'selected':''} ${off?'disabled':''}>${esc(text)}</option>`).join('')}</select></div>`;
const hasRecord=value=>!!value&&Object.keys(value).length>0;
export function canChangeOperationWorkpiece(view){
 const d=view.scene?.domain,s=setupOf(view),p=selectedPlacement(view);
 if(!s||!p||p.setupId!==s.id||s.locked)return false;
 if(hasRecord(s.machineSetup)||hasRecord(s.fixtureDesign)||hasRecord(s.registrationPlan)&&s.registrationPlan.status!=='Missing')return false;
 const g=s.generatedGeometry;
 if(g&&(g.operations?.length||g.transitions?.length||g.revision||g.resource||g.sampleGeometryRef||g.status&&!['Missing','Disabled'].includes(g.status)))return false;
 if((d.intentToolpaths||view.scene.intentToolpaths||[]).some(c=>c.setupId===s.id||c.placementId===p.id))return false;
 return ![...(d.fixtures||[]),...(d.fixtureUnits||[])].some(f=>f.setupId===s.id);
}
function referenceDraft(view){
 const draft=view.fiveOperationReferenceEdit,p=selectedPlacement(view);
 if(!draft)return null;
 if(draft.setupId!==setupOf(view)?.id||draft.placementId!==p?.id||draft.originalWorkpieceId!==p?.workpieceId||draft.epoch!==view.scene?.domain?.expected?.epoch){
  if(!view.fiveOperationRequest)view.fiveOperationReferenceEdit=null;
  return null;
 }
 return draft;
}
export function renderOperationList(view){
 const d=view.scene?.domain;if(!d)return '';
 const workpiece=activeWorkpiece(view),placements=workpiece?operationPlacements(view,{workpieceId:workpiece.id}):[],kind=preparationKind(view),busy=workpieceSelectionBusy(view),selected=selectedPlacement(view);
 const rows=placements.map(p=>{
  const s=d.setups.find(s=>s.id===p.setupId),w=d.workpieces.find(w=>w.id===p.workpieceId),units=(d.fixtureUnits||[]).filter(u=>u.setupId===s.id);
  const isSelected=selected?.id===p.id;
  return `<div class="five-operation-item" data-five-key="operation-${attr(s.id)}"><div class="five-preparation-node" data-five-setup-drag="${attr(s.id)}" draggable="${!busy}" title="拖动调整工序顺序">${renderSetupRow(view,p,isSelected&&kind!=='unit')}</div><div class="five-operation-reference">${esc(w?.name||'尚未选择工件')}</div>${isSelected&&units.length?`<div class="five-operation-fixtures" data-five-key="operation-fixtures-${attr(s.id)}"><div class="five-preparation-caption">夹具单元<span>${units.length}</span></div>${renderFixtureList(view,units,true)}</div>`:''}</div>`;
 }).join('');
 return `<section data-five-key="operation-list"><div class="five-fixture-heading"><strong>工序</strong><span>${placements.length}</span></div><div class="five-operation-list" aria-label="工序列表">${rows||'<div class="five-empty-row">当前工件尚未建立工序</div>'}</div><div class="five-tree-footer">${button('five.operation.create','新增工序',busy||!workpiece||workpiece.enabled===false?'disabled':'')}</div></section>`;
}
export function renderOperationParameters(view){
 const d=view.scene?.domain;if(!d)return '';
 const setup=setupOf(view),placement=selectedPlacement(view),s=machineState(view),busy=workpieceSelectionBusy(view)||s.busy;
 const current=placement?.setupId===setup?.id?placement:null;
 if(!setup||!current){const workpiece=activeWorkpiece(view);return section('operation-empty','工序',hint('当前工件尚无工序。点击“新增工序”并选择机型。')+button('five.operation.create','新增工序',busy||!workpiece||workpiece.enabled===false?'disabled':''));}
 const reference=current.workpieceId,editing=referenceDraft(view),allowed=canChangeOperationWorkpiece(view),ownBusy=!!(view.fiveOperationBusy||view.fiveOperationRequest);
 const choices=d.workpieces.map(w=>[w.id,w.name||'未命名工件',w.enabled===false]);
 const retry=view.fiveOperationRequest?hint('工序操作结果尚未确认，请重试原操作。')+button('five.operation.retry','重试原操作',view.pending||view.fiveOperationBusy?'disabled':''):'';
 const candidateValid=editing&&editing.workpieceId!==reference&&d.workpieces.some(w=>w.id===editing.workpieceId&&w.enabled!==false);
 const source=section('operation-reference','引用工件',editing
  ?control('workpieceId','工件',editing.workpieceId,choices,ownBusy)+`<div class="five-button-row">${button('five.operation.reference-apply','确定',!allowed||busy||!candidateValid?'disabled':'class="five-primary"')}${button('five.operation.reference-cancel','取消',ownBusy?'disabled':'')}</div>`+retry
  :`<p data-five-key="operation-reference-value">${esc(d.workpieces.find(w=>w.id===reference)?.name||'未命名工件')}</p>`+(allowed?button('five.operation.reference-edit','更换工件',busy||!d.workpieces.some(w=>w.enabled!==false&&w.id!==reference)?'disabled':''):'')+retry);
 if(activeTool(view)!=='machine-type')return source;
 const effective=setup.machineType||s.data?.effective||{},type=effective.machineType,name=machineTypeLabels[type]||effective.name||'尚未设置';
 const counts=type==='HeadHead'?[2,0]:type==='HeadTable'?[1,1]:type==='TableTable'?[0,2]:null;
 return source+section('operation-machine-type','机型',`<div data-five-operation-machine-effective>${facts([['实际使用',name]])}</div>`
  +(counts?facts([['直线轴','XYZ'],['刀头侧',counts[0]?`${counts[0]} 个转轴`:'固定'],['工件侧',counts[1]?`${counts[1]} 个转轴`:'固定']])
  :'')+hint('机型在新增工序时确定。需要其他机型时，请新增工序。')
  +hint(type==='HeadTable'||type==='TableTable'?'工装以导入专用夹具并定位为主；生成承托可作为辅助。':'可按当前装夹姿态添加夹具或生成支撑。')
  +(s.queryError?`<p class="five-hint" role="alert">${esc(s.queryError)} ${button('five.operation.type-refresh','重新读取',s.loading?'disabled':'')}</p>`:''));
}
async function refreshMachineTypes(context,view,ops){
 const s=machineState(view);if(!s.setupId||!active(context,view))return;
 const serial=++s.serial,key=queryKey(view),setupId=s.setupId;
 const current=()=>active(context,view)&&view.fiveMachineTypeAssignments===s&&setupOf(view)?.id===setupId&&s.serial===serial;
 s.loading=true;s.queryError='';if(view.fiveAxisArea==='operation')ops.renderProject(context,view);
 try{const data=await context.sceneProxy.invoke('CamOperation.MachineAssignments',{setupId});if(current()){s.data=data;s.loadedKey=key;}}
 catch(error){if(current()){s.queryError=error.message;s.loadedKey=key;}}
 finally{if(view.fiveMachineTypeAssignments===s&&s.serial===serial){s.loading=false;if(!current())s.loadedKey=null;if(current()&&view.fiveAxisArea==='operation')ops.renderProject(context,view);}}
}
export function syncMachineTypeAssignments(context,view,ops){
 const s=machineState(view);
 if(view.fiveAxisArea!=='operation'||view.fiveOperationCreateDialog||activeTool(view)!=='machine-type'||!active(context,view)){s.active=false;return;}
 if(!s.active){s.active=true;s.loadedKey=null;}
 if(s.setupId&&!s.loading&&!s.busy&&!s.request&&s.loadedKey!==queryKey(view))void refreshMachineTypes(context,view,ops);
}
export async function operationField(context,view,name,value,ops){
 if(!active(context,view))return;
 const creating=view.fiveOperationCreateDialog;
 if(creating){
  if(view.fiveOperationRequest||view.fiveOperationBusy)return;
  if(name==='machineType'&&(value===''||Object.hasOwn(machineTypeLabels,value)))creating.machineType=value;
  else return;
  view.error='';ops.renderProject(context,view);return;
 }
 const editing=referenceDraft(view);
 if(editing&&name==='workpieceId'){
  if(view.fiveOperationRequest||view.fiveOperationBusy||!view.scene.domain.workpieces.some(w=>w.id===value&&w.enabled!==false))return;
  editing.workpieceId=value;view.error='';ops.renderProject(context,view);return;
 }
 if(workpieceSelectionBusy(view))return;
}
async function manageOperation(context,view,action,ops,reference){
 if(!active(context,view)||view.fiveOperationBusy||workpieceSelectionBusy(view)&&!view.fiveOperationRequest)return;
 const creating=view.fiveOperationCreateDialog;
 const editing=referenceDraft(view),d=view.scene?.domain,setup=setupOf(view),workpieceId=action==='create'?(view.fiveOperationRequest?.workpieceId??creating?.workpieceId):view.fiveOperationRequest?.workpieceId??reference??editing?.workpieceId;
 if(action==='create'&&!view.fiveOperationRequest&&(!creating||creating.epoch!==d?.expected?.epoch||activeWorkpiece(view)?.id!==creating.workpieceId)){view.error='当前工件已变化，请关闭弹窗后重新新增工序。';ops.renderProject(context,view);return;}
 if(action==='reference'&&!view.fiveOperationRequest&&(!editing||!canChangeOperationWorkpiece(view)||workpieceId===editing.originalWorkpieceId))return;
 if(!view.fiveOperationRequest&&(!workpieceId||!d.workpieces.some(w=>w.id===workpieceId&&w.enabled!==false))){view.error='请选择本工序使用的工件。';ops.renderProject(context,view);return;}
 if(action==='create'&&!view.fiveOperationRequest&&!Object.hasOwn(machineTypeLabels,creating?.machineType||'')){view.error='请选择本工序使用的机型。';ops.renderProject(context,view);return;}
 if(!view.fiveOperationRequest)view.fiveOperationRequest={commandId:crypto.randomUUID(),expected:structuredClone(d.expected),action,workpieceId,...(action==='reference'?{setupId:setup.id}:{name:`工序 ${(d.setups?.length||0)+1}`,machineType:creating.machineType})};
 const request=view.fiveOperationRequest,placementId=action==='reference'?d.placements.find(p=>p.setupId===request.setupId)?.id:null;
 const ownsReference=()=>request.action==='reference'?setupOf(view)?.id===request.setupId&&selectedPlacement(view)?.id===placementId:view.scene?.domain?.expected?.epoch===request.expected.epoch&&activeWorkpiece(view)?.id===request.workpieceId;
 view.fiveOperationBusy=true;view.pending=true;view.error='';ops.renderProject(context,view);
 try{
  let result;try{result=await context.sceneProxy.invoke('CamOperation.Manage',request);}
  catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')view.fiveOperationRequest=null;throw error;}result=receipt.result;}
  view.fiveOperationRequest=null;if(request.action==='create')view.fiveOperationCreateDialog=null;else if(view.fiveOperationReferenceEdit===editing)view.fiveOperationReferenceEdit=null;await saveCommittedProjectEdit(context,view,ops);
  if(active(context,view)&&ownsReference()){resetWorkpieceTools(view);view.fiveActivePlacementId=result.placementId;if(await ops.refreshSceneState(context,view)===false)throw Error('工序已保存，请刷新以读取最新状态。');if(!active(context,view)||!ownsReference())return;view.fiveOperationPlacementId=result.placementId;(view.fiveCamPlacements??=new Map()).set(request.workpieceId,result.placementId);view.fivePreparationKind='setup';selectTool(view,'machine-type');await ops.fitViewAfterRenderPublish?.(context,view,{entityIds:[result.placementId]});if(active(context,view)&&ownsReference())ops.appendProjectLog?.(context,'ok',action==='create'?'已新增工序，引用共享工件几何。':'当前工序引用的工件已更新。');}
 }catch(error){if(active(context,view)&&ownsReference())view.error=error.message;}
 finally{view.fiveOperationBusy=false;view.pending=false;if(active(context,view))ops.renderProject(context,view);}
}
export async function operationCommand(context,view,action,ops){
 if(action==='retry'&&view.fiveOperationRequest)return manageOperation(context,view,view.fiveOperationRequest.action,ops,view.fiveOperationRequest.workpieceId);
 if(action==='create'){
  if(!active(context,view)||workpieceSelectionBusy(view)||view.fiveOperationCreateDialog)return;
  const workpiece=activeWorkpiece(view);if(!workpiece||workpiece.enabled===false){view.error=workpiece?'当前工件已禁用，请先在工件页启用。':'请先选择工件。';ops.renderProject(context,view);return;}
  view.fiveOperationCreateDialog={workpieceId:workpiece.id,workpieceName:workpiece.name||'未命名工件',epoch:view.scene.domain.expected.epoch,machineType:''};view.error='';ops.renderProject(context,view);return;
 }
 if(action==='create-cancel'){
  if(!active(context,view)||view.fiveOperationBusy||view.fiveOperationRequest)return;
  view.fiveOperationCreateDialog=null;view.error='';ops.renderProject(context,view);return;
 }
 if(action==='create-apply'){
  if(!view.fiveOperationCreateDialog)return;
  return manageOperation(context,view,'create',ops,view.fiveOperationCreateDialog.workpieceId);
 }
 if(view.fiveOperationCreateDialog)return;
 if(action==='reference-edit'){
  if(!active(context,view)||workpieceSelectionBusy(view)||referenceDraft(view)||!canChangeOperationWorkpiece(view))return;
  const p=selectedPlacement(view);if(!view.scene.domain.workpieces.some(w=>w.enabled!==false&&w.id!==p.workpieceId))return;
  view.fiveOperationReferenceEdit={setupId:p.setupId,placementId:p.id,originalWorkpieceId:p.workpieceId,workpieceId:p.workpieceId,epoch:view.scene.domain.expected.epoch};view.error='';ops.renderProject(context,view);context.mount?.querySelector?.('[data-five-operation-field="workpieceId"]')?.focus({preventScroll:true});return;
 }
 if(action==='reference-cancel'){
  if(!active(context,view)||view.fiveOperationRequest||view.fiveOperationBusy)return;
  view.fiveOperationReferenceEdit=null;view.error='';ops.renderProject(context,view);context.mount?.querySelector?.('[data-five-command="five.operation.reference-edit"]')?.focus({preventScroll:true});return;
 }
 if(action==='reference-apply'){
  const editing=referenceDraft(view);if(editing)return manageOperation(context,view,'reference',ops,editing.workpieceId);return;
 }
 if(action==='type-refresh')return refreshMachineTypes(context,view,ops);
}
