import { submitLeadEdit, leadErrorText } from './cutLeads.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import { escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { activeTool } from './workspaceModel.mjs';
import { button } from './workspaceControls.mjs';
import { nearestCutLine, selectCut } from './cutSelection.mjs';
import {revealSceneCut} from './sceneListReveal.mjs';

const snapshot=view=>JSON.stringify(view.scene?.domain?.expected);
const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const active=view=>view.fiveAxisArea==='toolpath'&&activeTool(view)==='path-start'&&view.fiveAxisStartOpen;
const errorText=error=>/CLOSED_CURVE_REQUIRED/.test(error.message)?'请选择闭合轮廓。'
  :/NOT_FOUND/.test(error.message)?'请靠近轮廓选点。'
  :/AMBIGUOUS/.test(error.message)?'这里有重合曲线，请换一个位置。'
  :/STALE|EXPECTED|SEQUENCE/.test(error.message)?'工程已变化，请重新选点。':leadErrorText(error);

export function openCutStarting(view) {
  view.fiveStart??={request:null};view.fiveAxisStartOpen=true;view.error='';
}
export function renderCutStarting(view) {
  if(!active(view))return '';
  const request=view.fiveStart?.request;
  return `<div class="five-start-prompt" data-five-key="start-prompt" data-five-tool="path-start" role="status"><span>${esc(view.error||'点击轮廓设置起点')}</span>
    ${request&&!view.pending?button('five.start.retry','重试'):''}
    ${button('five.start.close','×',`aria-label="退出起点设置" ${request?'disabled':''}`)}</div>`;
}
async function submit(context,view,state,ops) {
  const request=state.request;
  try {await submitLeadEdit(context,request);}
  catch(error){if(error.leadEditNotFound)state.request=null;throw error;}
  state.request=null;
  await saveCommittedCutEdit(context,view,ops);
  if(current(context,view))await ops.refreshSceneState(context,view);
  ops.appendProjectLog(context,'ok','刀路起点已修改');
}
export async function startCommand(context,view,action,ops) {
  const state=view.fiveStart;
  if(action==='close') {
    if(state?.request)return;
    view.fiveStart=null;view.fiveAxisStartOpen=false;view.error='';ops.renderProject(context,view);return;
  }
  if(action!=='retry'||!state?.request||view.pending)return;
  view.pending=true;view.error='';ops.renderProject(context,view);
  try {await submit(context,view,state,ops);}
  catch(error){if(current(context,view)&&active(view))view.error=errorText(error);}
  finally{view.pending=false;if(current(context,view))ops.renderProject(context,view);}
}
export function handleStartPick(context,view,_data,_hit,event,_hits,ops) {
  if(!active(view))return false;
  const state=view.fiveStart;
  if(view.pending||state?.request||!event)return true;
  const pick=nearestCutLine(view,event);if(!pick)return true;
  selectCut(view,pick.intentId);view.error='';
  const expected=view.scene.domain.expected,key=snapshot(view);
  view.pending=true;ops.renderProject(context,view);revealSceneCut(context,view,ops);
  void (async()=>{
    try {
      const result=await context.sceneProxy.invoke('CamCut.ResolveStart',{expected,...pick,mode:'ClosestPoint',frame:'Workpiece'});
      if(!current(context,view)||!active(view)||view.fiveStart!==state||key!==snapshot(view))return;
      state.request={commandId:crypto.randomUUID(),expected,intentIds:[pick.intentId],patch:{start:result.start}};
      ops.renderProject(context,view);
      await submit(context,view,state,ops);
    } catch(error){if(current(context,view)&&active(view)&&view.fiveStart===state)view.error=errorText(error);}
    finally{view.pending=false;if(current(context,view))ops.renderProject(context,view);}
  })();
  return true;
}
