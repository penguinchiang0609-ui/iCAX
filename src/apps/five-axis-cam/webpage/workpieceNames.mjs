import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';

export function editWorkpieceName(view,id,value){
  const w=view.scene?.domain?.workpieces?.find(w=>w.id===id);if(!w)return;
  const drafts=view.fiveWorkpieceNames??=new Map(),d=drafts.get(id)||{};
  d.value=value;d.dirty=value!==w.name;d.error='';drafts.set(id,d);
}
export function cancelWorkpieceName(view,id){
  const d=view.fiveWorkpieceNames?.get(id);if(!d?.request)view.fiveWorkpieceNames?.delete(id);
}
export async function saveWorkpieceName(context,view,id,ops){
  const d=view.fiveWorkpieceNames?.get(id);if(!d||!d.dirty&&!d.request||view.disposed||context.isCurrentProject?.()===false)return;
  if(view.pending){d.waiting=true;return;}
  const name=d.value.trim();
  if(!name){d.error='请填写工件名称。';d.waiting=false;ops.renderProject(context,view);return;}
  const request=d.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),workpieceId:id,name};
  d.waiting=false;d.error='';view.pending=true;ops.renderProject(context,view);
  try{
    try{await context.sceneProxy.invoke('CamWorkpiece.Rename',request);}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});if(receipt.status!=='Completed'){if(receipt.status==='NotFound')d.request=null;throw error;}}
    d.request=null;
    if(d.value.trim()===request.name)view.fiveWorkpieceNames.delete(id);else d.waiting=true;
    await saveCommittedProjectEdit(context,view,ops);
    if(!view.disposed&&context.isCurrentProject?.()!==false)await ops.refreshSceneState(context,view);
  }catch(error){d.error=d.request?'名称保存结果待确认，请重试。':error.message;}
  finally{view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
export function flushWaitingWorkpieceNames(context,view,ops){
  if(view.pending||view.disposed)return;
  for(const [id,d] of view.fiveWorkpieceNames||[])if(d.waiting){void saveWorkpieceName(context,view,id,ops);break;}
}
