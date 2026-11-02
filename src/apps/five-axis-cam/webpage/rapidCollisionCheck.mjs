import {button} from './workspaceControls.mjs';
import {escapeText as esc} from '../../_shared/workbench/utils/format.mjs';
import {manualRapidPath,manualRapidDraft} from './rapidPathEditing.mjs';
import {hasRapidGeometry} from './rapidGeometry.mjs';
import {waitForTaskResult} from './taskProgressStream.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';
import {rapidDependencyKey} from './rapidDependencies.mjs';

const geometry=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.generatedGeometry;
const key=rapidDependencyKey;
const collisionContextKey=view=>{
 const domain=view.scene?.domain,setup=domain?.setups?.find(s=>s.id===domain.activeSetupId),head=setup?.headModel,settings=setup?.rapidSettings;
 return JSON.stringify([setup?.headInstanceId,head?.instance?.revision,head?.tcpLocalPosition,head?.beamLocalDirection,head?.collisionModel,setup?.machineSetup,
  settings?.collisionThresholdMm,settings?.envelopeRadiusMm,settings?.envelopeLengthMm,
  domain?.workpieces?.map(w=>[w.id,w.geometryRef]),domain?.placements?.map(p=>[p.id,p.workpieceId,p.transform]),
  domain?.fixtures?.map(f=>[f.id,f.geometryRef,f.transform,f.enabled,f.collisionEnabled])]);
};
const pair=t=>`${t.from}/${t.to}`;
const policyKey=policy=>JSON.stringify(Object.entries(policy||{}).sort().map(([name,value])=>[name,String(value)]));
const source=view=>view.fiveRapid?.preview?.result||manualRapidDraft(view)||geometry(view)?.transitions?.find(t=>pair(t)===view.fiveRapid?.id);
export function storedRapidCollisionCheck(view,transition){
 const check=view.fiveRapidCollisionChecks?.get(pair(transition));
 return check&&check.key===key(view,pair(transition))&&check.collisionKey===collisionContextKey(view)&&check.policyKey===policyKey(transition.policy)?check:null;
}
export function forgetRapidCollisionChecks(view,ids){
 for(const id of ids){view.fiveRapidCollisionChecks?.delete(id);if(view.fiveRapid?.id===id)view.fiveRapid.collisionCheck=null;}
}
function displayedRapidCollisionCheck(view,transition){
 const state=view.fiveRapid,selected=state?.id===pair(transition);
 if(selected&&state.collisionCheck?.token===state.token&&state.collisionCheck.key===key(view)&&state.collisionCheck.collisionKey===collisionContextKey(view)&&state.collisionCheck.policyKey===policyKey(state.policy))return state.collisionCheck;
 // An edited or previewed route is different from the saved route covered by
 // the per-connection check, even before its native input key has changed.
 if(selected&&(state.preview||state.pathEdit?.changed||state.pathEdit?.gesture||state.hudEditing||state.policyChanged||state.automaticChanged||policyKey(state.policy)!==policyKey(transition.policy)))return null;
 return storedRapidCollisionCheck(view,transition);
}
export function rapidCollisionDisplay(view,transition){
 if(!transition||transition.endpointStatus==='Stale')return transition;
 const result=displayedRapidCollisionCheck(view,transition)?.result;
 if(!result)return transition;
 // This is a display projection of evidence only. In particular, a fixed-route
 // check must never install its returned sampling or alter the authored path.
 return {...transition,fixedRouteCheck:true,geometryStatus:result.geometryStatus,validation:result.validation,
  reason:result.reason||'',obstacleId:result.obstacleId||'',failedSegment:result.failedSegment};
}
function available(view){
 const state=view.fiveRapid,t=geometry(view)?.transitions?.find(t=>pair(t)===state?.id);
 return state?.key===key(view)&&(geometry(view)?.status==='Current'||t?.endpointStatus==='Current')&&hasRapidGeometry(source(view));
}
export function renderRapidCollisionCheck(view,errorText){
 const state=view.fiveRapid;if(!state||state.settings||!state.id)return '';
 const transition=geometry(view)?.transitions?.find(t=>pair(t)===state.id);
 const check=transition&&displayedRapidCollisionCheck(view,transition);
 const obstacle=check?.result?.obstacleId;
 const obstacleName=obstacle&&(view.scene.domain.fixtures?.find(f=>f.id===obstacle)?.name||view.scene.domain.workpieces?.find(w=>view.scene.domain.placements?.some(p=>p.id===obstacle&&p.workpieceId===w.id))?.name||'工件');
 return `<section class="five-rapid-collision" data-five-key="rapid-collision"><h4>当前空移碰撞检测</h4><p>使用 CAM 页配置的刀头检查工件、夹具；机床实际刀头及部件在机床仿真中检查。</p>${button('five.rapid.check',state.checking?'正在检测…':'检测碰撞',`class="five-primary" ${view.pending||state.request||state.running||!available(view)?'disabled':''}`)}${state.checking?button('five.rapid.cancel-check','取消检测'):''}${check?`<div role="status">${check.result?.validation==='Clear'?'当前刀头模型检测通过':check.result?'当前段未通过检测':esc(check.error||'检测未完成')}</div>${check.result?.reason?`<p>${esc(errorText(check.result.reason))}</p>`:''}${obstacleName?`<p>障碍物：${esc(obstacleName)}</p>`:''}`:''}</section>`;
}
export async function checkRapidCollision(context,view,ops,errorText){
 const state=view.fiveRapid;
 if(!state||state.settings||!state.id||state.running||state.request||view.pending||!available(view))return;
 const displayed=source(view),snapshot=key(view),collisionKey=collisionContextKey(view),id=state.id,storedRoute=!state.preview&&!state.pathEdit?.changed;
 state.cancel?.();const token=++state.token;
 const active=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveRapid===state&&state.open&&state.token===token&&state.id===id&&key(view)===snapshot&&collisionContextKey(view)===collisionKey;
 let task,cancelled=false;
 const cancel=()=>{cancelled=true;if(task?.status==='Running')void context.sceneProxy.invoke('CamRapid.CancelTask',{epoch:task.epoch,taskId:task.taskId}).catch(()=>{});};
 state.cancel=cancel;state.checking=true;state.running=true;state.collisionCheck=null;
 ops.renderProject(context,view);
 try{
  // A fixed manual route checks the displayed geometry, with no route search,
  // repository mutation, Apply command, file save or batch regeneration.
  let manual=manualRapidPath(view);
  if(!manual&&displayed.construction)manual={construction:structuredClone(displayed.construction)};
  if(!manual){
   if(displayed.points?.length!==displayed.directions?.length)throw Error('当前段缺少完整路径数据，无法检测碰撞。');
   manual={points:structuredClone(displayed.points.slice(1,-1)),directions:structuredClone(displayed.directions.slice(1,-1))};
  }
  const [from,to]=id.split('/'),policy=Object.fromEntries(Object.entries(state.policy).map(([name,value])=>{
   if(name==='strategy')return [name,value];
   if(String(value).trim()===''||!Number.isFinite(Number(value)))throw Error('请输入有效的空移参数。');
   return [name,Number(value)];
  }));
  task=await context.sceneProxy.invoke('CamRapid.CheckPath',{commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,geometryRevision:geometry(view).revision,from,to,policy,manualPath:manual},{timeoutMs:machiningTaskTimeoutMs});
  if(cancelled||!active()){cancel();return;}
  task=await waitForTaskResult(context,task,{active,queryMethod:'CamRapid.QueryStatus',cancelMethod:'CamRapid.CancelTask',timeoutMs:machiningTaskTimeoutMs});
  if(!active()||cancelled)return;
  if(task?.status!=='Completed')throw Error(task?.error||'本段碰撞检测未完成。');
  const result=Object.values(task.result.connections)[0]?.result;
  if(!result)throw Error('本段碰撞检测没有返回结果。');
  state.collisionCheck={token,key:snapshot,collisionKey,result,geometry:JSON.stringify(geometry(view).revision),policyKey:policyKey(state.policy)};
  if(storedRoute)(view.fiveRapidCollisionChecks??=new Map()).set(id,state.collisionCheck);
 }catch(error){if(active()&&!cancelled)state.collisionCheck={token,key:snapshot,collisionKey,policyKey:policyKey(state.policy),error:errorText(error)};}
 finally{if(view.fiveRapid===state&&state.token===token){state.checking=false;state.running=false;if(state.cancel===cancel)state.cancel=null;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}}
}
