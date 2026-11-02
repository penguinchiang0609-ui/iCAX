import { activeTool, workspaceState } from './workspaceModel.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { button } from './workspaceControls.mjs';
import { leadErrorText } from './cutLeads.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';

const categories=[['Leads','引线'],['Overcut','过切'],['Gap','缺口'],['Kerf','补偿']];
const selected=view=>{const ui=workspaceState(view);return (view.scene.intentToolpaths||[]).filter(c=>!c.locked&&(ui.selectedIntentIds||[ui.selectedIntentId]).includes(c.id));};
export function openCutClearing(view){view.fiveAxisClearOpen=true;view.fiveClear??={categories:categories.map(([id])=>id),request:null};}
export function renderCutClearing(view){
  const state=view.fiveClear,cuts=(view.scene.intentToolpaths||[]).filter(c=>!c.locked),ids=selected(view).map(c=>c.id),busy=view.pending||!!state?.request;
  return `<div class="five-position-dialog" data-five-key="clear-dialog" data-five-tool="clear" role="dialog" aria-label="清除工艺">
    <header class="five-position-title"><strong>清除工艺</strong>${button('five.clear.close','×',`aria-label="关闭清除工艺" ${busy?'disabled':''}`)}</header>
    <div class="five-position-body five-clear-body"><div class="five-clear-selection"><span>刀路</span><select aria-label="刀路" data-five-clear-select data-five-key="clear-selection" ${busy?'disabled':''}><option value="" ${ids.length!==1?'selected':''}>${ids.length?`已选 ${ids.length} 条`:'请选择'}</option>${cuts.map(c=>`<option value="${attr(c.id)}" ${ids.length===1&&ids[0]===c.id?'selected':''}>${esc(c.name)}</option>`).join('')}</select><label><input type="checkbox" data-five-command="five.clear.all" ${cuts.length>0&&ids.length===cuts.length?'checked':''} ${busy||!cuts.length?'disabled':''}>全选</label></div>
    <fieldset class="five-clear-categories" data-five-key="clear-categories"><legend>清除内容</legend>${categories.map(([id,title])=>`<label><input type="checkbox" data-five-command="five.clear.category.${id}" ${state?.categories.includes(id)?'checked':''} ${busy?'disabled':''}>${title}</label>`).join('')}</fieldset>
    ${view.error?`<div class="five-position-error" role="alert">${esc(view.error)}</div>`:''}</div>
    <footer class="five-position-actions">${button('five.clear.apply',state?.request?'重试':'确定',view.pending||(!state?.request&&(!ids.length||!state?.categories.length))?'disabled':'class="five-primary"')}${button('five.clear.close','取消',busy?'disabled':'')}</footer></div>`;
}
export async function clearCommand(context,view,action,ops){
  if(activeTool(view)!=='clear')return;
  const state=view.fiveClear;if(!state)return;
  if(action==='close'){
    if(view.pending||state.request)return;
    view.fiveAxisClearOpen=false;view.fiveClear=null;view.error='';ops.renderProject(context,view);return;
  }
  if(view.pending)return;
  if(action==='all'||action.startsWith('category.')){
    if(state.request)return;
    if(action==='all'){
      const ui=workspaceState(view),cuts=(view.scene.intentToolpaths||[]).filter(c=>!c.locked);
      ui.selectedIntentIds=selected(view).length===cuts.length?[]:cuts.map(c=>c.id);ui.selectedIntentId=ui.selectedIntentIds[0]||'';ui.selectedGroupId='';ui.selectedGroupIds=[];
    }else{
      const id=action.slice(9);if(!categories.some(([value])=>value===id))return;
      state.categories=state.categories.includes(id)?state.categories.filter(value=>value!==id):[...state.categories,id];
    }
    ops.renderProject(context,view);return;
  }
  if(action!=='apply'||!view.fiveAxisClearOpen)return;
  const ids=selected(view).map(c=>c.id);
  if(!state.request&&(!ids.length||!state.categories.length))return;
  state.request??={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentIds:ids,categories:[...state.categories]};
  const request=state.request,current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    try{await context.sceneProxy.invoke('CamProcess.Clear',request);}
    catch(error){
      let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')state.request=null;throw error;}
    }
    state.request=null;
    await saveCommittedCutEdit(context,view,ops);
    if(view.fiveClear===state){view.fiveClear=null;view.fiveAxisClearOpen=false;}
    ops.appendProjectLog(context,'ok',`已清除 ${request.intentIds.length} 条刀路的所选工艺`);
    if(current())await ops.refreshSceneState(context,view);
  }catch(error){
    if(current())view.error=state.request?'清除结果尚未确认，请点击“重试”确认。'
      :/EXPECTED|SEQUENCE|STALE/.test(error.message)?'工程已变化，请刷新后重试。'
      :/INTENT_SELECTION_INVALID/.test(error.message)?'每次最多选择 256 条刀路。':leadErrorText(error);
  }finally{view.pending=false;if(current())ops.renderProject(context,view);}
}
