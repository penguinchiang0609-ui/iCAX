import { activeTool, workspaceState } from './workspaceModel.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { buildCutTree, cutSequenceNumbers } from './cutTreeModel.mjs';
import { refreshMachiningState } from './domainActions.mjs';
import { renderToolAxisParameters } from './toolAxisParameters.mjs';
import { renderToolAxisPoints } from './toolAxisPoints.mjs';
import { renderToolAxisActions } from './toolAxisActions.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import {renderStandardHoleDimensions,renderManualHoleAction} from './standardHoleDimensions.mjs';

function draftFor(view,cut){
  const drafts=view.fiveCommandDrafts??=new Map();let draft=drafts.get(cut.id);
  if(!draft||(!draft.dirty&&!draft.request)){
    draft={beforeCommands:cut.beforeCommands||'',afterCommands:cut.afterCommands||'',dirty:false,request:null};drafts.set(cut.id,draft);
  }return draft;
}
export function renderCollisionPoseEditor(view){
  const ui=workspaceState(view),ids=ui.selectedIntentIds||[ui.selectedIntentId];
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===(view.fivePoseSelection?.active&&view.fivePoseSelection.selected?.intentId||(ids.length===1?ids[0]:null)));
  return `<div data-five-key="collision-pose-editor">${renderToolAxisActions(view)}${cut?(activeTool(view)==='keypoint'?renderToolAxisPoints(view,cut):renderToolAxisParameters(view,cut)):'<p>请选择一条刀路调整姿态。</p>'}</div>`;
}
export function renderCutParameters(view,{panel='all'}={}){
  const ui=workspaceState(view),ids=ui.selectedIntentIds||[ui.selectedIntentId],poseId=view.fivePoseSelection?.active&&view.fivePoseSelection.selected?.intentId,cut=view.scene?.intentToolpaths?.find(c=>c.id===(poseId||(ids.length===1?ids[0]:null)));
  if(!cut)return `<section class="five-cut-parameters" data-five-key="cut-parameters"><header>${panel==='commands'?'前后指令':'刀路参数'}</header><p>${ids.length>1?`已选 ${ids.length} 条刀路，请选择一条查看参数。`:'请选择一条刀路'}</p>${['all','axis','points'].includes(panel)?renderToolAxisActions(view):''}</section>`;
  const d=draftFor(view,cut),disabled=cut.locked||view.fivePoseSelection?.active?'disabled':'';
  const name=view.fiveNameDrafts?.get(cut.id),sequence=cutSequenceNumbers(buildCutTree(view)).get(cut.id);
  return `<section class="five-cut-parameters" data-five-key="cut-parameters"><header>${esc(cut.name)}</header>
    <label>刀路名称<input data-five-cut-name data-five-cut-id="${attr(cut.id)}" data-five-key="${attr(cut.id)}-name" aria-label="刀路名称" maxlength="128" value="${attr(name?.value??cut.name)}" ${disabled} data-model-value-authoritative="${!name?.request}"></label>
    <p>固定编号 #${esc(cut.number)} · ${cut.enabled!==false?`加工序号 ${sequence}`:'已禁用'}</p>${name?.error?`<p role="alert">${esc(name.error)} <button data-five-command="five.name.retry:${attr(cut.id)}">重试</button></p>`:''}
    ${cut.layers?.name?`<p data-five-key="cut-layer"><i class="five-tree-layer-color" style="background:${attr(cut.layers.color)}" aria-hidden="true"></i> 图层：${esc(cut.layers.name)}</p>`:''}
    ${['all','properties'].includes(panel)?`${cut.machiningKind==='Cut'?`<p data-five-key="cut-material-side">${cut.retainedSide==='Interior'?'材料：轮廓内 · 废料：轮廓外':cut.retainedSide==='Exterior'?'材料：轮廓外 · 废料：轮廓内':'尚未确定材料侧'}</p>`:''}${renderStandardHoleDimensions(view,cut)}${renderManualHoleAction(view,cut)}`:''}
    ${['all','axis','points'].includes(panel)?`${renderToolAxisActions(view)}${panel==='points'||panel==='all'&&activeTool(view)==='keypoint'?renderToolAxisPoints(view,cut):renderToolAxisParameters(view,cut)}`:''}
    ${['all','commands'].includes(panel)?`${['beforeCommands','afterCommands'].map((key,i)=>`<label>${i?'后指令':'前指令'}<textarea data-five-cut-command="${key}" data-five-cut-id="${attr(cut.id)}" data-five-key="${attr(cut.id)}-${key}" data-model-value-authoritative="${!d.dirty&&!d.request}" spellcheck="false" rows="5" ${disabled}>${esc(d[key])}</textarea></label>`).join('')}${d.error?`<p role="alert">${esc(d.error)} <button data-five-command="five.commands.retry:${attr(cut.id)}">重试</button></p>`:''}`:''}</section>`;
}
export function editCutName(view,id,value){
  const cut=view.scene?.intentToolpaths?.find(c=>c.id===id);if(!cut||cut.locked)return;
  const drafts=view.fiveNameDrafts??=new Map(),draft=drafts.get(id)??{};draft.value=value;draft.error='';drafts.set(id,draft);
}
export async function saveCutName(context,view,id,ops){
  if(view.disposed||!view.scene||context.isCurrentProject?.()===false)return;
  const draft=view.fiveNameDrafts?.get(id),cut=view.scene.intentToolpaths.find(c=>c.id===id);if(!draft||!cut||cut.locked)return;
  if(view.pending){draft.waiting=true;return;}
  const name=draft.value.trim();if(!name){draft.error='请输入刀路名称。';ops.renderProject(context,view);return;}
  if(name===cut.name&&!draft.request){view.fiveNameDrafts.delete(id);return;}
  const request=draft.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentId:id,name};
  const saveLatestAfterReceipt=name!==request.name;draft.waiting=false;view.pending=true;view.fiveNameSavingId=id;ops.renderProject(context,view);
  try{let result;try{result=await context.sceneProxy.invoke('CamCut.Rename',request);}catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')draft.request=null;throw error;}result=receipt.result;}
    draft.request=null;
    if(view.fiveNameDrafts.get(id)===draft){
      if(draft.value.trim()===request.name)view.fiveNameDrafts.delete(id);
      else draft.waiting=saveLatestAfterReceipt||draft.waiting;
    }
    await saveCommittedCutEdit(context,view,ops);await refreshMachiningState(context,view,ops,request,result);
  }catch(error){draft.error=draft.request?'名称保存结果待确认，请重试。':error.message;}
  finally{view.pending=false;view.fiveNameSavingId=null;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
export function editCutCommandText(view,id,key,value){
  const cut=view.scene.intentToolpaths.find(c=>c.id===id);if(!cut||cut.locked)return;
  const draft=draftFor(view,cut);draft[key]=value;draft.dirty=true;draft.error='';
}
export async function saveCutCommandText(context,view,id,ops){
  const d=view.fiveCommandDrafts?.get(id);if(!d||!d.dirty&&!d.request||view.disposed||context.isCurrentProject?.()===false)return;
  if(view.pending){d.waiting=true;return;}
  const request=d.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),intentId:id,beforeCommands:d.beforeCommands,afterCommands:d.afterCommands};
  d.request=request;d.waiting=false;d.error='';view.pending=true;view.fiveCommandSavingId=id;
  try{
    try{await context.sceneProxy.invoke('CamCut.SetCommands',request);}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}}
    d.request=null;d.dirty=d.beforeCommands!==request.beforeCommands||d.afterCommands!==request.afterCommands;
    d.waiting=d.dirty;
    await saveCommittedCutEdit(context,view,ops);
    if(!view.disposed&&context.isCurrentProject?.()!==false)await ops.refreshSceneState(context,view);
  }catch(error){d.error=d.request?'保存结果待确认。':/LOCKED/.test(error.message)?'刀路已锁定。':error.message;}
  finally{view.pending=false;view.fiveCommandSavingId=null;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
export function flushWaitingCommands(context,view,ops){
  if(view.pending||view.disposed)return;
  for(const [id,d] of view.fiveNameDrafts||[])if(d.waiting&&!d.error){d.waiting=false;queueMicrotask(()=>{void saveCutName(context,view,id,ops);});return;}
  for(const [id,d] of view.fiveCommandDrafts||[])if(d.waiting&&(d.dirty||d.request)){queueMicrotask(()=>{void saveCutCommandText(context,view,id,ops);});break;}
}
