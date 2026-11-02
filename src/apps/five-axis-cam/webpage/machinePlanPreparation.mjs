import {waitForTaskResult} from './taskProgressStream.mjs';
import {saveEditedProject} from './projectEditPersistence.mjs';
import {generationError} from './toolpathGeneration.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';
import {refreshMachinePlanState} from './machinePlanState.mjs';
import {rapidError} from './rapidPlanning.mjs';

const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
const usable=t=>['Generated','Draft'].includes(t.geometryStatus)&&t.endpointStatus!=='Stale';
// The same native cutting and rapid generators used by the CAM page own all
// prerequisites. This adapter only sequences their authoritative receipts.
export async function prepareMachinePlanTrajectory(context,view,s,ops,{cancel,progress}={}){
 const setupId=setupOf(view)?.id,active=()=>!view.disposed&&context.isCurrentProject?.()!==false&&setupOf(view)?.id===setupId;
 const refresh=async()=>active()&&await refreshMachinePlanState(context,view,ops)&&active();
 const run=async(kind,group,payload)=>{
  const errorText=error=>generationError(kind==='rapids'?rapidError(error):error);
  s.planPhase=kind;s.planTask=null;s.planTaskCancelMethod=group+'.CancelTask';s.planCancelPromise=null;
  ops.renderProject(context,view);
  const request=s.planPreparationRequest??={kind,payload:{commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId,...payload}};
  if(request.kind!==kind)throw Error('前次刀路准备结果尚未确认，请重试原操作。');
  let receipt;
  try{receipt=await context.sceneProxy.invoke(group+'.Generate',request.payload,{timeoutMs:machiningTaskTimeoutMs});}
  catch(error){if(/CAM_|CURVE_|LEAD_|PROCESS_|RAPID_|TRAVEL_/.test(error.message))s.planPreparationRequest=null;throw Error(errorText(error));}
  s.planTask=receipt;
  if(receipt.status==='Running'&&(s.planCancelRequested||!active()))await cancel?.();
  // Recovered terminal tasks may still own a live progress descriptor.
  let task=receipt.status!=='Running'&&receipt.taskId?await context.sceneProxy.invoke(group+'.QueryStatus',{epoch:receipt.epoch,taskId:receipt.taskId},{timeoutMs:machiningTaskTimeoutMs}):await waitForTaskResult(context,receipt,{active,cancel,queryMethod:group+'.QueryStatus',cancelMethod:group+'.CancelTask',timeoutMs:machiningTaskTimeoutMs,onProgress:value=>{s.planTask=value;progress?.();}});
  s.planTask=task;
  if(!task)return false;
  s.planPreparationRequest=null;
  if(task.status==='Cancelled'){s.error='已取消刀路准备，未开始机床运动规划。';return false;}
  if(task.status!=='Completed')throw Error(task.status==='Superseded'?'准备期间工程已变化，请重新规划。':errorText(task.error||'刀路准备失败。'));
  // Persist completed prerequisites even if the user left the page meanwhile.
  await saveEditedProject(context);
  if(!await refresh())return false;
  if(s.planCancelRequested){s.error='刀路准备已完成，已取消后续机床运动规划。';return false;}
  return true;
 };
 if(!await refresh())return false;
 if(s.planCancelRequested)return false;
 if(s.planPreparationRequest?.kind==='rapids'){
  if(!await run('rapids','CamRapid',{}))return false;
 }
 if(s.planPreparationRequest?.kind==='cutting'||setupOf(view)?.generatedGeometry?.status!=='Current'){
  if(!await run('cutting','CamPlan',{}))return false;
 }
 let generated=setupOf(view)?.generatedGeometry;
 if(generated?.status!=='Current'||!generated.operations?.length)throw Error('当前装夹还没有可规划的切割路径，请先建立刀路。');
 if(s.planPreparationRequest?.kind==='rapids'||generated.transitions?.some(t=>!usable(t))){
  if(!await run('rapids','CamRapid',{geometryRevision:generated.revision}))return false;
 }
 generated=setupOf(view)?.generatedGeometry;
 if(generated?.status!=='Current')throw Error('切割路径在准备期间已变化，请重新规划。');
 const failed=generated.transitions?.filter(t=>!usable(t))||[];
 if(failed.length)throw Error(`${failed.length} 段空移尚未形成可用连接，请在 CAM 页检查对应空移段。${failed[0].reason?' '+generationError(rapidError(failed[0].reason)):''}`);
 s.planPhase=null;s.planTask=null;s.planTaskCancelMethod=null;s.planCancelPromise=null;return active()&&!s.planCancelRequested;
}
