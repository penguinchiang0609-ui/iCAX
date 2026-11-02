import { escapeText as esc } from '../../_shared/workbench/utils/format.mjs';
import { activeTool, workspaceState } from './workspaceModel.mjs';
import { button } from './workspaceControls.mjs';
import { saveCommittedCutEdit } from './cutEditPersistence.mjs';
import { nearestCutLine, selectCut } from './cutSelection.mjs';
import {revealSceneCut} from './sceneListReveal.mjs';
import { waitForTaskResult } from './taskProgressStream.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';

const snapshot=view=>JSON.stringify(view.scene?.domain?.expected);
const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const errorText=error=>{
  const text=error.message;
  if(/CLEAR_CLOSURE/.test(text))return '请先清除所选刀路的过切或缺口，再打断。';
  if(/SPLIT_ENDPOINT/.test(text))return '这里已是刀路端点，请在线内选点。';
  if(/SPLIT_REQUIRES_CUTTING_BOUNDARY/.test(text))return '请在切割轮廓上选点，引入线和引出线不能作为断点。';
  if(/EXISTING_START/.test(text))return '这里已是刀路起点，请选择其他位置。';
  if(/DUPLICATE|DEGENERATE/.test(text))return '断点重复或过近，请重新选点。';
  if(/NOT_FOUND/.test(text))return '请靠近刀路选点。';
  if(/AMBIGUOUS/.test(text))return '这里有重合曲线，请换一个位置选点。';
  if(/STALE|EXPECTED|SEQUENCE|REVISION_CONFLICT/.test(text))return '工程已变化，请重新选择断点。';
  return text;
};
export function openCutSplitting(view) {view.fiveSplit={key:snapshot(view),points:[],request:null};view.fiveAxisSplitOpen=true;view.error='';}
export function renderCutSplitting(view) {
  const state=view.fiveSplit,busy=!!view.pending;
  return `<div class="five-position-dialog" data-five-key="split-dialog" data-five-tool="split" role="dialog" aria-label="打断刀路">
    <header class="five-position-title"><strong>打断刀路</strong>${button('five.split.close','×',`aria-label="关闭打断刀路" ${busy&&state?.request?'disabled':''}`)}</header>
    <div class="five-position-body"><p class="five-split-hint">点击刀路上的关键点或任意位置，点击确定打断。</p>
      ${view.error?`<div class="five-position-error" role="alert">${esc(view.error)}</div>`:''}</div>
    <footer class="five-position-actions">${button('five.split.confirm',state?.request&&!busy?'重试':'确定',busy||(!state?.points.length&&!state?.request)?'disabled':'class="five-primary"')}${button('five.split.close','取消',busy&&state?.request?'disabled':'')}</footer>
  </div>`;
}
export function handleSplitPick(context,view,_data,_hit,event,_hits,ops) {
  if(activeTool(view)!=='split'||!view.fiveAxisSplitOpen)return false;
  const state=view.fiveSplit;
  if(view.pending||state?.picking||state?.request||!event)return true;
  if(state.key!==snapshot(view)){state.points=[];state.key=snapshot(view);}
  const pick=nearestCutLine(view,event);if(!pick)return true;
  selectCut(view,pick.intentId);state.picking=true;view.pending=true;view.error='';ops.renderProject(context,view);revealSceneCut(context,view,ops);
  const resolve=()=>context.sceneProxy.invoke('CamCut.ResolveSplitPoint',{expected:view.scene.domain.expected,...pick,mode:'Auto',frame:'Workpiece'},{timeoutMs:machiningTaskTimeoutMs});
  void resolve().catch(async error=>{
    if(!/CAM_REVISION_CONFLICT/.test(error.message)||!current(context,view)||view.fiveSplit!==state||!view.fiveAxisSplitOpen)throw error;
    // Reconcile a late native publication before repeating this read-only pick.
    // Existing picks belong to the old snapshot and cannot be carried forward.
    if(await ops.refreshSceneState(context,view)===false)throw error;
    if(!current(context,view)||view.fiveSplit!==state||!view.fiveAxisSplitOpen)throw error;
    state.points=[];state.key=snapshot(view);view.pending=true;return resolve();
  }).then(result=>{
    if(!current(context,view)||view.fiveSplit!==state||!view.fiveAxisSplitOpen||state.key!==snapshot(view))return;
    const index=state.points.findIndex(p=>p.intentId===pick.intentId&&p.anchor.segmentId===result.anchor.segmentId&&Math.abs(p.anchor.parameter-result.anchor.parameter)<1e-8);
    if(index>=0)state.points.splice(index,1);
    else if(state.points.length<256)state.points.push({intentId:pick.intentId,...result});
  }).catch(error=>{if(current(context,view)&&view.fiveSplit===state&&view.fiveAxisSplitOpen)view.error=errorText(error);}).finally(()=>{
    state.picking=false;view.pending=!!view.sceneRefreshPending;if(current(context,view))ops.renderProject(context,view);
  });
  return true;
}
export async function splitCommand(context,view,action,ops) {
  if(action==='close'){if(view.pending&&view.fiveSplit?.request)return;view.fiveAxisSplitOpen=false;view.fiveSplit=null;view.error='';ops.renderProject(context,view);return;}
  const state=view.fiveSplit;
  if(action!=='confirm'||!state||view.pending)return;
  if(!state.request){
    if(state.key!==snapshot(view)){state.points=[];state.key=snapshot(view);view.error='工程已变化，请重新选择断点。';ops.renderProject(context,view);return;}
    if(!state.points.length)return;
    const byCut=new Map();for(const p of state.points){if(!byCut.has(p.intentId))byCut.set(p.intentId,[]);byCut.get(p.intentId).push(p.anchor);}
    state.request={commandId:crypto.randomUUID(),expected:view.scene.domain.expected,cuts:[...byCut].map(([intentId,points])=>({intentId,points}))};
  }
  state.committing=true;view.pending=true;view.error='';ops.renderProject(context,view);
  try {
    let result;
    try {result=await context.sceneProxy.invoke('CamCut.Split',state.request,{timeoutMs:machiningTaskTimeoutMs});}
    catch(error){const receipt=await context.sceneProxy.invoke('CamScene.QueryCommand',{epoch:state.request.expected.epoch,commandId:state.request.commandId});
      if(receipt.status!=='Completed'){state.request=null;throw error;}result=receipt.result;}
    state.request=null;await saveCommittedCutEdit(context,view,ops);
    if(result.taskId){
      const task=await waitForTaskResult(context,result,{active:()=>current(context,view),queryMethod:'CamWorkpiece.QueryTask',cancelMethod:'CamWorkpiece.CancelTask'});
      if(!task||task.status!=='Completed')throw Error(task?.error||'刀路编辑已保存，派生结果尚未生成。');
      result=task.result;await saveCommittedCutEdit(context,view,ops);
    }
    await ops.refreshSceneState(context,view);
    if(!current(context,view))return;
    if(view.fiveSplit===state){
      const ui=workspaceState(view);ui.selectedIntentIds=result.intentIds;ui.selectedIntentId=result.intentIds[0];
      view.fiveAxisSplitOpen=false;view.fiveSplit=null;
    }
    ops.appendProjectLog(context,'ok','刀路已打断');
  } catch(error){if(current(context,view)&&view.fiveAxisSplitOpen)view.error=errorText(error);}
  finally{state.committing=false;view.pending=!!view.sceneRefreshPending;if(current(context,view))ops.renderProject(context,view);}
}
