import {toggleWorkpieceLock} from './workpieceActions.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {activeTool,currentDraft,selectTool} from './workspaceModel.mjs';
import {selectedPlacement} from './registrationActions.mjs';
import {resetWorkpieceTools,workpieceSelectionBusy,selectWorkpiece} from './workpieceSelection.mjs';
import {button,section,hint,field} from './workspaceControls.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';

export const currentSetup=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const setupIcon=(view,action,label,path,disabled=false,pressed=false)=>`<button type="button" class="five-setup-icon" data-five-command="five.setup.${action}" title="${label}" aria-label="${label}" aria-pressed="${pressed}" ${workpieceSelectionBusy(view)||disabled?'disabled':''}><svg viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg></button>`;
export function renderSetupRow(view,x,selected=x.id===selectedPlacement(view)?.id,label=null){
  const s=view.scene.domain.setups.find(s=>s.id===x.setupId),off=s.enabled===false,busy=workpieceSelectionBusy(view);
  return `<div class="five-setup-row ${selected?'selected':''} ${off?'is-disabled':''}" data-five-key="setup-${attr(x.id)}">${view.fiveSetupNameEdit===x.id?`<input class="five-setup-select five-setup-name-inline" type="text" data-five-setup-name data-five-key="setup-inline-name" aria-label="工序名称" value="${attr(currentDraft(view,'setup').setupName)}" ${busy?'readonly':''}>`:(label??`<button type="button" class="five-setup-select" title="双击修改名称" data-five-command="five.setup.activate:${attr(x.id)}" aria-pressed="${selected}" ${busy?'disabled':''}>${esc(s.name||'工序')}${off?'（已禁用）':''}</button>`)}<div class="five-setup-icons">${setupIcon(view,'lock:'+x.id,s.locked?'解锁工序':'锁定工序',s.locked?'M7 10V7a5 5 0 0 1 10 0v3 M5 10h14v11H5z M12 14v3':'M7 10V7a5 5 0 0 1 9-3 M5 10h14v11H5z M12 14v3',false,s.locked)}${setupIcon(view,'toggle:'+x.id,off?'启用工序':'禁用工序','M12 2v10 M6 5a9 9 0 1 0 12 0',false,off)}</div></div>`;
}
export function renderSetupSelector(view){
  const p=selectedPlacement(view);if(!p)return '';
  const items=view.scene.domain.setups.flatMap(s=>{const placement=view.scene.domain.placements.find(x=>x.setupId===s.id);return placement?[placement]:[];});
  const s=view.scene.domain.setups.find(s=>s.id===p.setupId);if(!s)return '';
  const busy=workpieceSelectionBusy(view),off=s.enabled===false;
  const chooser=`<details class="five-setup-chooser" data-five-key="setup-chooser" data-five-setup-chooser><summary aria-label="选择当前工序" aria-disabled="${busy}" tabindex="${busy?-1:0}" data-five-setup-label="${attr(p.id)}" title="双击修改名称"><strong>${esc(s.name||'工序')}${off?'（已禁用）':''}</strong><span aria-hidden="true">⌄</span></summary><div class="five-setup-options" aria-label="工序列表">${items.map(x=>{const setup=view.scene.domain.setups.find(s=>s.id===x.setupId);return `<button type="button" class="five-setup-select" data-five-key="setup-option-${attr(x.id)}" data-five-command="five.setup.activate:${attr(x.id)}" aria-pressed="${x.id===p.id}" ${busy?'disabled':''}>${esc(setup.name||'工序')}${setup.enabled===false?'（已禁用）':''}</button>`;}).join('')}</div></details>`;
  const row=renderSetupRow(view,p,true,chooser);
  return `<section class="five-section" data-five-key="setup-selector" aria-label="当前工序">${row}</section>`;
}
export function renderSetupManager(view){
  const s=currentSetup(view);if(!s)return '';
  const d=currentDraft(view,'setup');if(d.setupId!==s.id){d.setupId=s.id;d.setupName=s.name;}
  const count=view.scene.domain.placements.filter(p=>p.workpieceId===selectedPlacement(view)?.workpieceId).length;
  return section('setup-name','当前工序',field(d,'setupName','名称',{text:true})+button('five.setup.rename','保存名称',view.pending?'disabled':''))
    +section('setup-lifecycle','工序管理',button('five.operation.create','新增工序',view.pending?'disabled':'')+button('five.setup.remove','删除当前工序',view.pending||s.locked?'disabled':'')
      +hint('工序引用共享工件几何，机型、姿态、工装、三点定位资料和刀路各自独立。删除当前工序会移除它的工装和刀路，可撤销。'));
}
export function renderPoseLock(view){
  const s=currentSetup(view);if(!s)return '';
  const busy=workpieceSelectionBusy(view),disabled=busy||s.enabled===false;
  return `<div class="five-workpiece-actions" data-five-key="setup-actions"><strong>当前工序</strong><div class="five-setup-tools">${button('five.tool.translate','放置',disabled||s.locked?'disabled':'')}${button('five.tool.registration','三点定位',disabled?'disabled':'')}</div></div>`;

}
export async function setupCommand(context,view,action,ops){
  if(/^(lock|toggle|remove):/.test(action)){
    if(workpieceSelectionBusy(view))return;
    const [kind,id]=action.split(':');
    if(selectedPlacement(view)?.id!==id)await selectWorkpiece(context,view,id,ops);
    if(selectedPlacement(view)?.id!==id||view.error)return;
    selectTool(view,'translate');
    if(kind==='lock'){await toggleWorkpieceLock(context,view,ops);if(!view.disposed&&context.isCurrentProject?.()!==false){selectTool(view,'translate');ops.renderProject(context,view);}return;}
    action=kind==='toggle'?(currentSetup(view).enabled===false?'enable':'disable'):'remove';
  }
  if(action.startsWith('activate:'))return selectWorkpiece(context,view,action.slice(9),ops);
  if(workpieceSelectionBusy(view)&&!view.fiveSetupRequest)return;
  const p=selectedPlacement(view);if(!p)return;
  const s=currentSetup(view),alive=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  if(action==='edit-name'){const d=currentDraft(view,'setup');d.setupId=s.id;d.setupName=s.name;view.fiveSetupNameEdit=p.id;ops.renderProject(context,view);const input=context.mount.querySelector('[data-five-setup-name]');input?.focus({preventScroll:true});input?.select();return;}
  if(action==='cancel-name'){view.fiveSetupNameEdit=null;currentDraft(view,'setup').setupName=s.name;ops.renderProject(context,view);return;}
  if(!view.fiveSetupRequest){
    const count=view.scene.domain.setups.length;
    const name=action==='rename'?String(currentDraft(view,'setup').setupName||'').trim():`工序 ${count+1}`;
    if(action==='rename'&&!name){view.error='请填写工序名称。';ops.renderProject(context,view);return;}
    view.fiveSetupRequest={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),placementId:p.id,action,...(action==='remove'?{}:{name})};
  }
  const request=view.fiveSetupRequest;view.pending=true;view.error='';ops.renderProject(context,view);
  try{
    let result;try{result=await context.sceneProxy.invoke('CamSetup.Manage',request);}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')view.fiveSetupRequest=null;throw error;}result=receipt.result;}
    view.fiveSetupRequest=null;await saveCommittedProjectEdit(context,view,ops);
    if(alive()){if(action==='rename')view.fiveSetupNameEdit=null;if(action!=='rename')resetWorkpieceTools(view);view.fiveActivePlacementId=result.placementId;
      await ops.refreshSceneState(context,view);if(view.fiveAxisArea==='operation'){selectTool(view,'machine-type');view.fivePreparationKind='setup';if(result.setupId)view.fiveOperationPlacementId=result.placementId;}if(['create','remove'].includes(action))await ops.fitViewAfterRenderPublish(context,view,{entityIds:[result.placementId]});
      ops.appendProjectLog(context,'ok',action==='create'?'已新增工序，共用原工件几何。':action==='remove'?'当前工序已删除，可撤销。':action==='enable'?'当前工序已启用。':action==='disable'?'当前工序已禁用，不参与仿真和输出。':`工序已命名为 ${request.name}。`);}
  }catch(error){if(alive())view.error=error.message;}
  finally{view.pending=false;if(alive())ops.renderProject(context,view);}
}

