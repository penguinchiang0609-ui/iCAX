import {collisionChecks,selectedCollisionCheck,collisionCounts} from './collisionLocations.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {acceptCollisionProgress,animateCollisionProgress,collisionActivity,disposeCollisionProgress,updateCollisionActivity} from './collisionProgress.mjs';
import {machiningTaskTimeoutMs} from './operationTiming.mjs';
import {disposeCollisionDiagnostics,updateCollisionRepair} from './collisionDiagnostics.mjs';
import {waitForTaskResult} from './taskProgressStream.mjs';
import {sceneColors} from './scenePalette.mjs';
import {rapidSegmentDistances} from './simulationTrace.mjs';
import {readMachiningState,mergeDomainState} from './domainActions.mjs';

const setupOf=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
export function syncCollision(view,s) {
  const report=setupOf(view)?.simulationCheck;
  const key=JSON.stringify(report);
  if(key===s.collisionKey)return;
  if(!s.repairing&&s.repairResultExpected!==JSON.stringify(view.scene?.domain?.expected))s.repairResult=null;
  s.collisionKey=key;s.collisionReport=null;
  disposeCollision(s);
  for(const frame of s.frames)delete frame.collision;
  if(report?.validity!=='Current')return;
  // Never attach results to a different playback path, even after a stale reply.
  if(report.frames?.length!==s.frames.length||report.frames.some((f,i)=>!Array.isArray(f.position)||s.frames[i].position.distanceTo(new THREE.Vector3(...f.position))>1e-6||s.frames[i].rotation.angleTo(new THREE.Quaternion(...f.rotation))>1e-6))return;
  s.collisionReport=report;
  for(const segment of report.segments??[]){const frame=s.frames[Number(segment.index)];if(frame)frame.collision=segment;}
}
export function collisionLabel(s,belongs=null) {
  if(s.checking)return collisionActivity(s).stage;
  const r=s.collisionReport;if(!r)return s.frameContact?(s.frameContact.status==='Unchecked'?`当前帧未检测 · ${s.frameContact.reason}`:s.frameContact.status==='Collision'?'当前帧 · 碰撞':'当前帧 · 未发现碰撞'):'尚无整条轨迹检测报告';
  const scope=r.scope==='BuiltinSchematicWorkpieceAndFixtures'?'示意头 · ':'';
  const counts=collisionCounts(r,belongs?row=>belongs(s.frames[Number(row.index)]):undefined);
  const labels=[counts.collisions&&`${counts.collisions} 处碰撞`,counts.limits&&`${counts.limits} 处限位内无解`,counts.unchecked&&`${counts.unchecked} 处未验证`,counts.skipped&&`${counts.skipped} 个相邻区间待复检`].filter(Boolean);
  return scope+(labels.join(' · ')||(counts.total?'碰撞与限位检测通过':belongs?'本条尚无检测结果':'尚无检测结果'));
}
export function disposeCollision(s){disposeCollisionDiagnostics(s);s.collisionOverlay?.removeFromParent();s.collisionOverlay?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});s.collisionOverlay=null;}
export function paintCollision(view,s) {
  const segment=s.frameContact??s.alarmCollision??s.frames[Math.max(0,Math.min(Math.floor(s.cursor),s.frames.length-2))]?.collision;
  s.group?.traverse(o=>{if(!o.isMesh||o.name==='nozzle-tcp')return;const material=o.material;if(!material?.color)return;
    o.userData.collisionOriginalColor??=material.color.getHex();material.color.setHex(s.frameContact?.mode==='DiscreteBoxes'&&segment?.status==='Collision'?sceneColors.collision:o.userData.collisionOriginalColor);});
  if(!s.collisionReport||!view.viewport)return;
  if(!s.collisionOverlay){
    const group=new THREE.Group(),rapidDistances=rapidSegmentDistances(s.frames);group.name='five-simulation-collisions';s.collisionOverlay=group;
    for(const [status,color] of [['Collision',sceneColors.collision],['Unchecked',0xc58d21]])for(const rapid of [false,true]){
      const points=[],distances=[];for(const segment of s.collisionReport.segments){if(segment.status!==status)continue;const i=Number(segment.index),a=s.frames[i],b=s.frames[i+1];
        // A missing connector is not geometry. Cuts follow the workpiece
        // contact path, while rapids follow TCP and retain their dashed style.
        if(!a||!b||b.breakBefore||(a.role==='Rapid')!==rapid)continue;
        const from=rapid?a.position:a.target,to=rapid?b.position:b.target;
        if(from&&to&&from.distanceToSquared(to)>1e-20){points.push(from,to);if(rapid)distances.push(...rapidDistances.get(i));}}
      if(points.length){const options={color,depthTest:false,depthWrite:false},geometry=new THREE.BufferGeometry().setFromPoints(points);if(rapid)geometry.setAttribute('lineDistance',new THREE.Float32BufferAttribute(distances,1));const line=new THREE.LineSegments(geometry,rapid?new THREE.LineDashedMaterial({...options,dashSize:3,gapSize:2}):new THREE.LineBasicMaterial(options));line.name=`collision-${status}${rapid?'-rapid':''}`;line.renderOrder=70;line.raycast=()=>{};group.add(line);}
    }
    view.viewport.scene.add(group);
  }
}
export function jumpCollision(s,forward) {
  const checks=collisionChecks(s.collisionReport)?.filter(r=>r.status==='Collision'||r.reason==='CAM_HEAD_LIMIT_NO_SOLUTION');
  if(checks?.length){
    checks.sort((a,b)=>Number(a.index)-Number(b.index)||Number(a.fraction)-Number(b.fraction));
    const at=checks.findIndex(r=>r.kind===s.selectedCollisionCheck?.kind&&r.key===s.selectedCollisionCheck?.key);
    const next=at>=0?checks[(at+(forward?1:checks.length-1))%checks.length]:checks.find(r=>Number(r.index)>=s.cursor)||checks[0];
    s.selectedCollisionCheck={kind:next.kind,key:next.key};s.cursor=Number(next.index)+(Number(next.fraction)||0);return;
  }
  const segments=(s.collisionReport?.segments??[]).filter(r=>r.status==='Collision');
  // Consecutive red segments form one issue; navigation visits their first hit.
  const hits=segments.filter((r,i)=>!i||Number(r.index)>Number(segments[i-1].index)+1).map(r=>Number(r.index)+Math.max(0,Math.min(1-1e-6,Number(r.fraction))));
  if(!hits.length)return;
  s.cursor=forward?(hits.find(i=>i>s.cursor+1e-6)??hits[0]):(hits.findLast(i=>i<s.cursor-1e-6)??hits.at(-1));
}
export async function checkCollision(context,view,s,ops,active,onPose,scope=null) {
  if(s.checking)return false;
  const requested={setupId:view.scene.domain.activeSetupId,...scope},pending=s.collisionRequest??s.collisionPreparationRequest;
  if(pending&&['setupId','intentId','from','to'].some(key=>pending[key]!==requested[key])){
    s.message=pending.intentId||pending.to?'上一次单条检测结果尚未确认，请先回到该刀路重试。':'上一次全局检测结果尚未确认，请先点击顶部碰撞检测重试。';
    ops.renderProject(context,view);return false;
  }
  const initialCursor=s.cursor;
  s.checkScope=scope?'SelectedPath':'AllPaths';
  s.checking=true;s.checkProgress=0;s.checkExactProgress=null;s.checkStage='preparing';s.collisionCancelRequested=false;s.message='';view.pending=true;
  ops.renderProject(context,view);
  try{
    if(scope?.intentId&&!s.collisionRequest){
      const setup=setupOf(view),operation=setup?.generatedGeometry?.operations?.find(op=>op.intentId===scope.intentId);
      if(operation?.status!=='Current'){
        s.collisionPreparationRequest??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:setup.id,intentId:scope.intentId};
        let generation=await context.sceneProxy.invoke('CamPlan.Generate',s.collisionPreparationRequest,{timeoutMs:machiningTaskTimeoutMs});
        let cancellation;s.cancelCheck=()=>cancellation??=context.sceneProxy.invoke('CamPlan.CancelTask',{epoch:generation.epoch,taskId:generation.taskId});
        if(s.collisionCancelRequested||!active())await s.cancelCheck();
        generation=await waitForTaskResult(context,generation,{active,cancel:()=>s.cancelCheck(),queryMethod:'CamPlan.QueryStatus',cancelMethod:'CamPlan.CancelTask',timeoutMs:machiningTaskTimeoutMs});
        if(!generation)return false;
        s.collisionPreparationRequest=null;
        if(generation.status==='Cancelled'){s.message='碰撞检测已取消';return false;}
        if(generation.status!=='Completed')throw Error(generation.error||'当前刀路姿态读取失败。');
        s.selectedPreparation=generation.result;
        const state=await readMachiningState(context,undefined,{waitForGeneration:false});if(!active())return false;
        mergeDomainState(view.scene,state);ops.renderProject(context,view);
      }
    }
    if(s.collisionCancelRequested||!active()){s.message='碰撞检测已取消';return false;}
    s.collisionRequest??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,...scope};
    ops.renderProject(context,view);
    animateCollisionProgress(context,view,s,active,onPose);
    let task=await context.sceneProxy.invoke('CamSimulation.CheckCollision',s.collisionRequest,{timeoutMs:machiningTaskTimeoutMs});s.collisionTask=task;
    s.checkStage='checking';acceptCollisionProgress(s,task);if(active()){updateCollisionActivity(context,s);onPose?.();}
    let cancelPromise;s.cancelCheck=()=>cancelPromise??=context.sceneProxy.invoke('CamSimulation.CancelTask',{epoch:task.epoch,taskId:task.taskId});
    if(s.collisionCancelRequested)await s.cancelCheck();
    task=await waitForTaskResult(context,task,{active,cancel:()=>s.cancelCheck(),timeoutMs:machiningTaskTimeoutMs,onProgress:progress=>{
      s.collisionTask=progress;acceptCollisionProgress(s,progress);
      updateCollisionActivity(context,s);onPose?.();const label=context.mount?.querySelector('[data-five-collision-status]');if(label)label.textContent=collisionLabel(s);
    }});
    if(!task)return false;s.collisionTask=task;acceptCollisionProgress(s,task);
    s.collisionRequest=null;
    if(task.status==='Cancelled'){s.cursor=initialCursor;s.checkExactProgress=null;s.message='碰撞检测已取消';if(active())onPose?.();return false;}
    if(task.status!=='Completed'){
      if(task.status==='Superseded'&&active())await ops.refreshSceneState(context,view);
      throw Error(task.status==='Superseded'?'工程已变化，请重新检测。':task.error||'碰撞检测失败。');
    }
    // Publication already committed natively. Persist its captured project
    // even when a project switch raced the final acknowledgement.
    s.checkStage='publishing';if(active())updateCollisionActivity(context,s);
    await saveCommittedProjectEdit(context,view,ops);
    if(active()){
      if(view.fiveWorkpieceSaveError)s.message=view.fiveWorkpieceSaveError;
      if(active())await ops.refreshSceneState(context,view);return true;
    }
  }catch(error){
    const message=error?.message||String(error);
    if(/CAM_|MOTION_/.test(message)){s.collisionRequest=null;s.collisionPreparationRequest=null;}
    s.message=message.includes('HEAD_CALIBRATION_REQUIRED')?'请先标定切割头。':message.includes('GEOMETRY_REQUIRED')?'请先重新生成刀路。':message.includes('REVISION_CONFLICT')?'工程已变化，请刷新后重新检测。':s.collisionRequest?'检测结果尚未确认，请重试。':message;
  }finally{disposeCollisionProgress(s);s.cancelCheck=null;s.checking=false;s.checkStage='';view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
  return false;
}
export async function repairCollision(context,view,s,ops,active){
 if(s.repairing||s.checking)return false;
 const report=s.collisionReport,selected=selectedCollisionCheck(s)||report?.segments?.find(r=>Number(r.index)===Math.floor(s.cursor)&&r.status==='Collision'),segmentIndex=Number(selected?.index);if(report?.validity!=='Current'||!selected)return false;
 const rapid=s.frames[segmentIndex]?.role==='Rapid',draft=s.repairDeviationDraft??String(s.repairDeviationDeg??15),angle=Number(draft);
 const translation=s.repairTranslationDraft??String(s.repairTranslationMm??5),distance=Number(translation);
 if(!String(draft).trim()||!Number.isFinite(angle)||angle<0||angle>180||!String(translation).trim()||!Number.isFinite(distance)||distance<0){s.message='请输入 0～180° 的角度容差和不小于 0 的位置容差。';ops.renderProject(context,view);return false;}
 s.repairing=true;s.repairCancelRequested=false;s.repairTask=null;s.repairResult=null;s.message='';view.pending=true;
 const frame=s.frames[segmentIndex];s.repairFocus={index:segmentIndex,intentId:frame?.intentId,role:frame?.role,target:frame?.target?.toArray()};
 try{
  s.repairRequest??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:view.scene.domain.activeSetupId,segmentIndex:String(segmentIndex),maxDeviationDeg:angle,maxTranslationMm:distance,localPoseDeviationAuthorized:true,...(selected.kind==='keypose'?{keyposeFrameIndex:selected.key}:selected.kind==='interval'?{intervalFirstFrameIndex:selected.key}:{})};
  ops.renderProject(context,view);
  let task=await context.sceneProxy.invoke('CamSimulation.AutoRepairCollision',s.repairRequest,{timeoutMs:machiningTaskTimeoutMs});s.repairTask=task;
  let cancelPromise;s.cancelRepair=()=>cancelPromise??=context.sceneProxy.invoke('CamSimulation.CancelTask',{epoch:task.epoch,taskId:task.taskId});
  if(s.repairCancelRequested)await s.cancelRepair();
  task=await waitForTaskResult(context,task,{active,cancel:()=>s.cancelRepair(),timeoutMs:machiningTaskTimeoutMs,
   onProgress:progress=>{s.repairTask=progress;updateCollisionRepair(context,s);}});
  if(!task)return false;s.repairTask=task;
  s.repairRequest=null;
  if(task.status==='Cancelled'){s.message='自动修复已取消；未应用候选姿态。';return false;}
  if(task.status!=='Completed')throw Error(task.status==='Superseded'?'工程已变化，请重新检测后修复。':task.error||'自动修复未完成。');
  const result=task.result;
  if(!result||!['Repaired','Unresolved'].includes(result.status)||typeof result.changed!=='boolean'||result.changed&&result.status!=='Repaired')throw Error('自动修复返回的结果无效，请刷新工程确认。');
  s.repairResult=result;
  // The native command owns its transaction. Only a committed repair reaches
  // the shared writer; unsuccessful searches never create a file/history edit.
  if(result.changed){await saveCommittedProjectEdit(context,view,ops);if(active())await ops.refreshSceneState(context,view);}
  s.repairResultExpected=JSON.stringify(view.scene.domain.expected);
  return result.changed;
 }catch(error){s.message=error?.message||String(error);if(/CAM_|MOTION_/.test(s.message))s.repairRequest=null;return false;}
 finally{s.cancelRepair=null;s.repairing=false;view.pending=false;if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);}
}
