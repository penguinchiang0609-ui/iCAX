import { workspaceState } from './workspaceModel.mjs';
import { leadErrorText } from './cutLeads.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';

const selected=view=>{const ui=workspaceState(view);return (view.scene.intentToolpaths||[]).filter(c=>!c.locked&&(ui.selectedIntentIds||[ui.selectedIntentId]).includes(c.id));};
export async function reverseCommand(context,view,action,ops) {
  if(action!=='apply')return;
  if(view.pending)return;
  if(view.fiveAxisLeadsOpen||view.fiveStart?.request||view.fiveHole?.request||view.fiveLeads?.request){
    view.error='请先完成或取消当前编辑。';ops.renderProject(context,view);return;
  }
  const ids=selected(view).map(c=>c.id);
  if(!view.fiveReverseRequest&&!ids.length){view.error='请先选择刀路。';ops.renderProject(context,view);return;}
  const request=view.fiveReverseRequest||{commandId:crypto.randomUUID(),expected:view.scene.domain.expected,intentIds:ids};
  view.fiveReverseRequest=request;view.pending=true;view.error='';
  const current=()=>!view.disposed&&context.isCurrentProject?.()!==false;
  ops.renderProject(context,view);
  try {
    try{await context.sceneProxy.invoke('CamCut.Reverse',request);}
    catch(error){
      let receipt;try{receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:request.expected.epoch,commandId:request.commandId});}catch{throw error;}
      if(receipt.status!=='Completed'){if(receipt.status==='NotFound')view.fiveReverseRequest=null;throw error;}
    }
    view.fiveReverseRequest=null;
    await saveCommittedCutEdit(context,view,ops);
    ops.appendProjectLog(context,'ok',`${request.intentIds.length} 条刀路已反向`);
    if(current())await ops.refreshSceneState(context,view);
  } catch(error){
    if(current())view.error=view.fiveReverseRequest?'反向结果尚未确认，请再次点击“反向”确认。'
      :/EXPECTED|SEQUENCE|STALE/.test(error.message)?'工程已变化，请刷新后重试。':leadErrorText(error);
  } finally{view.pending=false;if(current())ops.renderProject(context,view);}
}
