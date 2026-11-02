import {workpieceSelectionBusy,resetWorkpieceTools} from './workpieceSelection.mjs';
import {workpieceLocked,workpieceEnabled} from './workpieceState.mjs';
import {placementCommand,placementMatrix} from './workpiecePlacement.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';

export async function savePlacementDraft(context,view,ops){
  const placement=view.scene?.domain?.placements?.find(p=>p.id===view.scene.activePlacementId),draft=view.fivePlacement;
  if(draft?.oneShot){
    while(draft.saving)await new Promise(resolve=>setTimeout(resolve,20));
    if(draft.request)throw Error('缩放结果尚未确认，请先重试执行缩放。');
    return;
  }
  if(!workpieceLocked(view)&&draft?.open&&placement){
    const matrix=placementMatrix(draft).clone().transpose().toArray();
    if(matrix.some((v,i)=>Math.abs(v-placement.transform[i])>1e-9)){
      await placementCommand(context,view,'apply',ops);
      if(view.error||view.fivePlacement?.request)throw Error(view.error||'请先确认工件摆放结果。');
    }
  }
}

export async function removeWorkpiece(context,view,ops){
  if(view.disposed||context.isCurrentProject?.()===false||view.pending||view.viewport?.transformManipulator?.pointerId!=null)return;
  const d=view.fiveWorkpieceRemoval??={};
  if(!d.request){
    if(workpieceSelectionBusy(view)||workpieceLocked(view))return;
    const placement=view.scene?.domain?.placements?.find(p=>p.id===view.scene.activePlacementId);if(!placement)return;
    d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:placement.setupId,placementId:placement.id};
    resetWorkpieceTools(view);
  }
  const request=d.request,current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  view.pending=true;d.error='';view.error='';ops.renderProject(context,view);
  try{
    let result;
    try{result=await context.sceneProxy.invoke('CamWorkpiece.Remove',request);}
    catch(error){
      const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}
      result=receipt.result;
    }
    d.request=null;await saveCommittedProjectEdit(context,view,ops);
    if(current()){
      resetWorkpieceTools(view);view.fiveActivePlacementId=result.nextPlacementId;
      if(await ops.refreshSceneState(context,view)===false)throw Error('工件已删除，请刷新场景。');
      if(result.nextPlacementId)await ops.fitViewAfterRenderPublish(context,view,{entityIds:[result.nextPlacementId]});
      ops.appendProjectLog(context,'ok','工件已删除，可通过撤销恢复。');
    }
  }catch(error){if(current()){d.error=d.request?'删除结果尚未确认，请点击“核对删除结果”。':/REVISION|STALE/.test(error.message)?'工程已变化，请刷新后重试。':error.message;view.error=d.error;}}
  finally{view.pending=false;if(current())ops.renderProject(context,view);}
}

export async function toggleWorkpieceLock(context,view,ops){
  if(view.disposed||context.isCurrentProject?.()===false||view.pending||view.viewport?.transformManipulator?.pointerId!=null)return;
  const d=view.fiveWorkpieceLock??={};
  if(!d.request){
    if(workpieceSelectionBusy(view))return;
    const placement=view.scene?.domain?.placements?.find(p=>p.id===view.scene.activePlacementId);if(!placement)return;
    // Commit a visible transform draft before protecting this exact pose.
    try{await savePlacementDraft(context,view,ops);}catch(error){view.error=error.message;d.error=error.message;ops.renderProject(context,view);return;}
    d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),placementId:placement.id,locked:!workpieceLocked(view)};
    resetWorkpieceTools(view);
  }
  const request=d.request,current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  view.pending=true;d.error='';view.error='';ops.renderProject(context,view);
  try{
    try{await context.sceneProxy.invoke('CamSetup.SetLocked',request);}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}}
    d.request=null;await saveCommittedProjectEdit(context,view,ops);
    if(current()){if(await ops.refreshSceneState(context,view)===false)throw Error('装夹锁状态已保存，请刷新场景。');ops.appendProjectLog(context,'ok',request.locked?'当前装夹姿态已锁定，刀路可继续编辑。':'当前装夹姿态已解锁。');}
  }catch(error){if(current()){d.error=d.request?'结果尚未确认，请点击“核对锁定结果”。':error.message;view.error=d.error;}}
  finally{view.pending=false;if(current())ops.renderProject(context,view);}
}

export async function toggleWorkpieceEnabled(context,view,ops){
  if(view.disposed||context.isCurrentProject?.()===false||view.pending||view.viewport?.transformManipulator?.pointerId!=null)return;
  const d=view.fiveWorkpieceEnable??={};
  if(!d.request){
    if(workpieceSelectionBusy(view))return;
    const placement=view.scene?.domain?.placements?.find(p=>p.id===view.scene.activePlacementId);if(!placement)return;
    try{await savePlacementDraft(context,view,ops);}catch(error){view.error=error.message;d.error=error.message;ops.renderProject(context,view);return;}
    d.request={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),placementId:placement.id,enabled:!workpieceEnabled(view)};
    resetWorkpieceTools(view);
  }
  const request=d.request,current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  view.pending=true;d.error='';view.error='';ops.renderProject(context,view);
  try{
    try{await context.sceneProxy.invoke('CamWorkpiece.SetEnabled',request);}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}}
    d.request=null;await saveCommittedProjectEdit(context,view,ops);
    if(current()){
      if(await ops.refreshSceneState(context,view)===false)throw Error('启用状态已保存，请刷新场景。');
      ops.appendProjectLog(context,'ok',request.enabled?'工件已启用。':'工件已禁用，不参与加工；模型与刀路保留。');
    }
  }catch(error){if(current()){d.error=d.request?'结果尚未确认，请点击“核对启用状态”。':error.message;view.error=d.error;}}
  finally{view.pending=false;if(current())ops.renderProject(context,view);}
}
