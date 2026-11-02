import {saveEditedProject} from './projectEditPersistence.mjs';
import {waitForTaskResult} from './taskProgressStream.mjs';
import {hydrateMachinePlan} from './machinePlanResource.mjs';
import {setMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature} from './machineCollisionFeedback.mjs';
import {prepareMachinePlanTrajectory} from './machinePlanPreparation.mjs';
import {refreshMachinePlanState} from './machinePlanState.mjs';
import {presentMachinePlanIssue} from './machinePlanIssue.mjs';

const current=(context,view)=>!view.disposed&&context.isCurrentProject?.()!==false;
const setupOf=view=>view.scene?.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId);
const planKey=view=>JSON.stringify([setupOf(view)?.id,setupOf(view)?.machineMotionPlanRef,view.scene?.domain?.expected]);
export function machinePlanningActivity(s){
 if(s.planPhase)return s.planCancelRequested?'正在取消刀路准备':s.planPhase==='cutting'?'正在更新切割路径':'正在准备工序间空移';
 if(s.planHydrating)return s.planCancelRequested?'正在取消规划':'正在读取完整轴运动计划';
 const p=s.planTask?.machineProgress||{},stage=({Solving:'正在求解关键姿态',Selecting:'正在选择连续轴运动',Refining:'正在校验插值并补点',CheckingMotion:'正在检查运动和避让',Timing:'正在规划加减速',Compiling:'正在生成连续轴轨迹',CheckingDynamics:'正在验证轴动态上限',Saving:'正在保存运动计划'})[p.stage]||'正在准备机床与路径';
 const at=Number(p.currentStation),total=Number(p.totalStations),count=['Solving','Refining','CheckingMotion'].includes(p.stage)&&Number.isInteger(at)&&at>=0&&Number.isInteger(total)&&total>0?` ${Math.min(total,at+1)} / ${total}`:'';
 return s.planCancelRequested?'正在取消规划':stage+count;
}
function paintProgress(context,s){const node=context.mount.querySelector('[data-machine-plan-stage]');if(node)node.textContent=machinePlanningActivity(s);}
export async function loadSavedMachinePlan(context,view,s,ops){
 const key=planKey(view);if(s.planLoading||s.busy||s.dirty||!s.snapshot?.modelKey)return;
 const adopt=report=>{
  if(!report)return;
  s.planValidity=report.validity;
  if(report.validity==='Current'&&report.playbackAllowed===true&&report.modelKey===s.snapshot?.modelKey){
   s.planLoadedKey=key;
   if(s.report!==report){s.report=report;s.cursor=0;s.alarm=false;ops.renderProject(context,view);}
  }
 };
 // The cached native body and the installed playback report have separate
 // lifetimes. Selecting the current job may clear playback without changing
 // either the saved resource or the model it certifies.
 if(s.planReadKey===key){adopt(s.planReadReport);return;}
 if(!setupOf(view)?.machineMotionPlanRef?.revision)return;
 s.planLoading=true;
 try{
  const metadata=await context.sceneProxy.invoke('CamSimulation.ReadMachinePlan',{setupId:setupOf(view).id},{timeoutMs:120000});
  if(!current(context,view)||planKey(view)!==key)return;
  const report=await hydrateMachinePlan(metadata,context.sceneProxy,{active:()=>current(context,view)&&planKey(view)===key});
  if(!report)return;
  s.planReadKey=key;s.planReadReport=report;adopt(report);
 }catch(error){if(current(context,view)&&planKey(view)===key){s.error=error.message;ops.renderProject(context,view);}}
 finally{
  s.planLoading=false;
  // A new source may have arrived while the old read owned the loading flag.
  // Only the latest current source is retried, never the discarded response.
  if(current(context,view)&&planKey(view)!==key)await loadSavedMachinePlan(context,view,s,ops);
 }
}
export async function cancelMachinePlan(context,s){
 s.planCancelRequested=true;if(context.isCurrentProject?.()!==false&&s.planCurrent?.()!==false)paintProgress(context,s);const task=s.planTask;
 if(task?.status==='Running')return await (s.planCancelPromise??=context.sceneProxy.invoke(s.planTaskCancelMethod||'CamSimulation.CancelTask',{epoch:task.epoch,taskId:task.taskId}));
}
export async function planMachineMotion(context,view,s,ops){
 const geometrySignature=machineCollisionFeedbackGeometrySignature(view,s.machineId);
 const setupId=setupOf(view)?.id;s.planning=true;s.planCurrent=()=>current(context,view);s.planCancelRequested=false;s.planCancelPromise=null;s.planTask=null;s.planValidity=null;
 ops.renderProject(context,view);
 try{
  if(!s.planRequest&&!await prepareMachinePlanTrajectory(context,view,s,ops,{cancel:()=>cancelMachinePlan(context,s),progress:()=>paintProgress(context,s)}))return;
  if(!current(context,view)||setupOf(view)?.id!==setupId||s.planCancelRequested)return;
  s.planRequest??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId};
  const inputExpected=JSON.stringify(s.planRequest.expected);
  let task;
  try{task=await context.sceneProxy.invoke('CamSimulation.PlanMachine',s.planRequest,{timeoutMs:120000});}
  catch(error){if(/CAM_|CURVE_|LEAD_|PROCESS_|MOTION_/.test(error.message))s.planRequest=null;throw error;}
  s.planTask=task;
  if(s.planCancelRequested)await cancelMachinePlan(context,s);
  task=await waitForTaskResult(context,task,{active:()=>current(context,view),cancel:()=>cancelMachinePlan(context,s),
   onProgress:progress=>{s.planTask=progress;paintProgress(context,s);}});
  if(!task)return;s.planTask=task;
  s.planRequest=null;
  if(task.status==='Cancelled'){s.error='运动规划已取消。';return;}
  if(task.status!=='Completed')throw Error(task.status==='Superseded'?'工程已变化，请重新规划。':task.error||'运动规划未完成。');
  let metadata=await context.sceneProxy.invoke('CamSimulation.ReadMachineAttempt',{epoch:task.epoch,taskId:task.taskId},{timeoutMs:120000});
  if(!metadata||typeof metadata.playbackAllowed!=='boolean')throw Error('运动计划结果无效，请重新规划。');
  if(metadata.placementPreview===true||metadata.validity==='Preview')throw Error('这是摆放预览，请先应用摆放并重新规划。');
  if(metadata.playbackAllowed){await saveEditedProject(context);if(!current(context,view)||setupOf(view)?.id!==setupId||!await refreshMachinePlanState(context,view,ops))return;}
  if(!current(context,view))return;
  if(setupOf(view)?.id!==setupId)return;
  const authoritySource=planKey(view);
  if(metadata.playbackAllowed){
   // Saving and refreshing are asynchronous. Re-read native validity after
   // them; a task receipt alone cannot certify a later geometry/post edit.
   const authority=await context.sceneProxy.invoke('CamSimulation.ReadMachinePlan',{setupId},{timeoutMs:120000});
   if(!current(context,view)||planKey(view)!==authoritySource)return;
   const referenceKey=ref=>JSON.stringify(ref?.revision);
   if(authority.validity!=='Current'||!authority.playbackAllowed||authority.modelKey!==metadata.modelKey||!metadata.revision?.revision||!authority.revision?.revision||referenceKey(authority.revision)!==referenceKey(metadata.revision))throw Error('工程或运动计划已变化，请重新规划。');
   metadata=authority;
  }else if(JSON.stringify(view.scene.domain.expected)!==inputExpected)throw Error('工程已变化，原候选结果已放弃，请重新规划。');
  s.planHydrating=true;paintProgress(context,s);
  const report=await hydrateMachinePlan(metadata,context.sceneProxy,{active:()=>current(context,view)&&setupOf(view)?.id===setupId&&planKey(view)===authoritySource&&!s.planCancelRequested});
  if(!report)return;
  s.report=report;s.snapshot=report;s.cursor=0;s.alarm=false;s.revision=JSON.stringify(view.scene.domain.expected);s.planReadKey=planKey(view);s.planReadReport=report;s.planLoadedKey=report.validity==='Current'&&report.playbackAllowed?planKey(view):null;s.planValidity=report.validity;
  setMachineCollisionFeedback(view,s.machineId,report.rejectedMotion?.collisionPairs?report.rejectedMotion:report,{geometrySignature});
  presentMachinePlanIssue(view,report);
  if(!report.playbackAllowed){s.error=report.reason||'没有找到已通过检查的连续轴运动。';ops.appendProjectLog?.(context,'error',s.error);}
 }finally{s.planning=false;s.planHydrating=false;s.planTask=null;s.planCurrent=null;s.planPhase=null;s.planTaskCancelMethod=null;}
}
