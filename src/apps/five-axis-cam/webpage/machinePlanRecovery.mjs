import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machineForward,machineMatrix} from './machineKinematics.mjs';
import {machineAssemblyParts,machineAssemblyCommand} from './machineAssemblyPlacement.mjs';
import {waitForTaskResult} from './taskProgressStream.mjs';
import {hydrateMachinePlan} from './machinePlanResource.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {button} from './workspaceControls.mjs';
import {escapeText as esc} from '../../_shared/workbench/utils/format.mjs';
import {activeTool} from './workspaceModel.mjs';

const setupOf=v=>v.scene?.domain?.setups?.find(s=>s.id===v.scene.domain.activeSetupId);
const signature=v=>JSON.stringify([v.scene?.domain?.expected,setupOf(v)?.id,setupOf(v)?.machineSetup]);
const failure=v=>v.fiveMachine?.report?.playbackAllowed===false&&v.fiveMachine.report.failedTarget;
const point=p=>Array.isArray(p)&&p.length===3&&p.every(Number.isFinite);
const row=m=>m.clone().transpose().toArray();
const available=(context,v,s)=>!v.disposed&&context.isCurrentProject?.()!==false&&v.fiveAxisArea==='simulation'&&activeTool(v)==='machine-simulation'&&signature(v)===s.signature;
function clearVisuals(s){
 s.marker?.removeFromParent();s.marker?.traverse(o=>{o.geometry?.dispose();for(const m of Array.isArray(o.material)?o.material:[o.material])m?.dispose();});s.marker=null;
 for(const id of s.previewIds||[])s.viewport?.clearTransformPreview?.(id);s.previewIds=[];
}
export function disposeMachinePlanRecovery(view){const s=view.fiveMachineRecovery;if(!s)return;s.cancelled=true;s.candidate=null;s.located=false;clearVisuals(s);void s.cancel?.().catch(()=>{});}
function state(view){
 const key=signature(view);if(view.fiveMachineRecovery?.signature!==key){disposeMachinePlanRecovery(view);view.fiveMachineRecovery={signature:key,previewIds:[]};}
 return view.fiveMachineRecovery;
}
export function renderMachinePlanRecovery(view){
 const target=failure(view);if(!target||!point(target.positionMm)||!point(target.beamDirection))return '';
 const s=state(view),busy=!!s.running||view.pending||!!view.fiveMachine?.busy;
 const path=view.scene.domain.intentToolpaths?.find(p=>p.id===target.intentId),label=path?`${path.name||'刀路'} ${path.number||''}`.trim():'刀路';
 const location=target.role==='Rapid'?`进入${label}的空移`:`${label}的切割位置`;
 const reason=view.fiveMachine.error||view.fiveMachine.report.reason||'',budget=/budget exhausted|budget exceeded/i.test(reason);
 let status=budget?'本次计算尚未完成这段运动的验证，可定位当前检查位置。':'可以定位问题位置，调整工件与工装的整体摆放，或检查其他摆放位置。';
 if(s.running)status=`正在检查摆放 ${s.index+1} / ${s.candidates.length}${s.task?.machineProgress?.stage==='Solving'?' · 检查刀路姿态':' · 检查完整运动'}，原摆放保持不变。`;
 else if(s.candidate)status='找到一个通过当前整条刀路运动检查的摆放。画面正在预览，尚未应用。';
 else if(s.message)status=s.message;
 const actions=s.running?button('five.machine.recovery:cancel','取消检查'):s.candidate?button('five.machine.recovery:apply','应用摆放并重新规划')+button('five.machine.recovery:discard','取消预览'):budget?button('five.machine.recovery:locate','定位当前检查位置'):button('five.machine.recovery:locate','定位问题')+button('five.machine.recovery:adjust','调整摆放')+button('five.machine.recovery:search',s.request?'重试检查':'检查可行摆放');
 const detail=budget?'本次运动规划达到计算限额，尚未生成可播放的轨迹。':reason;
 return `<div class="five-machine-recovery" data-five-key="machine-plan-recovery"><strong>${s.candidate?'候选摆放预览':budget?'运动检查未完成':'这处刀路尚未找到可用的机床姿态'}</strong><span> · ${esc(location)}</span><p role="status" data-machine-recovery-status>${esc(status)}</p><div>${busy&&!s.running?actions.replaceAll('<button ','<button disabled '):actions}</div><details data-five-key="machine-recovery-detail"><summary>详细原因</summary><p>${esc(detail)}</p></details></div>`;
}
function showTarget(view,s,focus){
 const report=view.fiveMachine.report,t=report?.failedTarget,vp=view.viewport;if(!point(t?.positionMm)||!point(t?.beamDirection)||!vp?.scene)return;
 s.viewport=vp;if(!s.marker){
  const marker=new THREE.Group();marker.name='MachineUnresolvedTarget';marker.userData={independentInteractionVisual:true,machinePlanRecovery:true};
  const dot=new THREE.Mesh(new THREE.SphereGeometry(2.5,16,12),new THREE.MeshBasicMaterial({color:0xff4b35,depthTest:false,depthWrite:false}));dot.renderOrder=2001;marker.add(dot);
  const direction=new THREE.Vector3(...t.beamDirection).normalize();const arrow=new THREE.ArrowHelper(direction,new THREE.Vector3(...t.positionMm).addScaledVector(direction,-40),40,0xffc239,9,5);for(const part of [arrow.line,arrow.cone]){part.geometry=part.geometry.clone();part.material.depthTest=false;part.material.depthWrite=false;part.renderOrder=2000;}marker.add(arrow);
  marker.traverse(o=>{o.raycast=()=>{};o.userData.independentInteractionVisual=true;o.frustumCulled=false;});s.marker=marker;
 }
 const support=vp.sceneObjects?.get(report.configuration.supportId);if(!support)return;if(s.marker.parent!==support)support.add(s.marker);
 s.marker.children[0].position.set(...t.positionMm);s.marker.updateWorldMatrix(true,true);
 if(focus){const center=s.marker.children[0].getWorldPosition(new THREE.Vector3()),camera=vp.getCameraState?.();if(camera)vp.setCameraState({...camera,target:{x:center.x,y:center.y,z:center.z},radius:Math.min(camera.radius,350)});}
 vp.invalidate?.();
}
export function machinePlacementCandidates(report){
 const c=report?.configuration;if(!c||!Array.isArray(report.nodes)||!Array.isArray(report.positions))return [];
 const world=machineForward(report,report.positions),support=world[report.support],chain=[];let i=report.tool;
 while(i>=0){chain.unshift(i);i=report.nodes[i].parent;}
 const root=chain.find(k=>report.nodes[k].type!=='fixed');if(root===undefined)return [];
 const anchor=new THREE.Vector3().setFromMatrixPosition(world[root]).applyMatrix4(support.clone().invert()),base=machineMatrix(c.placement),origin=new THREE.Vector3().setFromMatrixPosition(base),toward=anchor.sub(origin);toward.z=0;
 if(toward.length()<1e-6)return [];toward.normalize();
 const result=[];for(const distance of [50,75,100,125,150,200]){const placement=base.clone();placement.setPosition(origin.clone().addScaledVector(toward,distance));result.push({...structuredClone(c),placement:row(placement)});}
 for(const angle of [Math.PI/2,-Math.PI/2]){const pivot=origin.clone().addScaledVector(toward,100),rotation=new THREE.Matrix4().makeRotationZ(angle),placement=rotation.multiply(base);placement.setPosition(pivot);result.push({...structuredClone(c),placement:row(placement)});}
 return result;
}
function preview(view,s){
 if(!s.candidate)return;const vp=view.viewport,support=vp?.sceneObjects?.get(s.candidate.configuration.supportId);if(!support)return;
 support.updateWorldMatrix(true,false);const parent=support.matrixWorld,placement=machineMatrix(s.candidate.configuration.placement);s.viewport=vp;
 for(const part of machineAssemblyParts(view)){vp.setTransformPreview?.(part.id,parent.clone().multiply(placement).multiply(machineMatrix(part.transform)).elements);}s.previewIds=machineAssemblyParts(view).map(p=>p.id);
}
export function syncMachinePlanRecovery(view){
 const s=view.fiveMachineRecovery;if(!s)return;
 if(s.signature!==signature(view)||view.disposed||view.fiveAxisArea!=='simulation'||activeTool(view)!=='machine-simulation'||!failure(view)){disposeMachinePlanRecovery(view);return;}
 if(s.located)showTarget(view,s,false);if(s.candidate)preview(view,s);
}
export async function machinePlanRecoveryCommand(context,view,action,ops,{replan}={}){
 const s=state(view),render=()=>{if(available(context,view,s))ops.renderProject(context,view);};
 if(action==='cancel'){s.cancelled=true;await s.cancel?.();render();return;}
 if(s.running||view.pending||view.fiveMachine?.busy)return;
 if(action==='locate'){s.located=true;showTarget(view,s,true);render();return;}
 if(action==='discard'){s.candidate=null;s.message='已取消预览，保留原摆放。';clearVisuals(s);render();return;}
 if(action==='adjust'){clearVisuals(s);s.candidate=null;s.located=true;showTarget(view,s,true);machineAssemblyCommand(context,view,'assembly:translate',ops);return;}
 if(action==='apply'){
  if(!s.candidate||!available(context,view,s))return;
  const configuration=structuredClone(s.candidate.configuration);view.fiveMachine.busy=true;
  try{
   await context.sceneProxy.invoke('CamSimulation.ConfigureMachine',{commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:setupOf(view).id,configuration},{timeoutMs:120000});
   clearVisuals(s);await saveCommittedProjectEdit(context,view,ops,'fiveMachineRecoverySaveError');
   await ops.refreshSceneState(context,view);view.fiveMachine.setupKey=null;view.fiveMachine.snapshotKey=null;view.fiveMachine.report=null;
  }catch(error){if(context.isCurrentProject?.()!==false){view.fiveMachine.error=error.message;ops.renderProject(context,view);}return;}
  finally{view.fiveMachine.busy=false;}
  if(!view.disposed&&context.isCurrentProject?.()!==false)await replan?.();return;
 }
 if(action!=='search'||!failure(view)||!available(context,view,s))return;
 s.candidates??=machinePlacementCandidates(view.fiveMachine.report);s.index=s.request?s.index||0:0;s.cancelled=false;s.running=true;s.candidate=null;s.message='';view.fiveMachine.busy=true;clearVisuals(s);render();
 const deadline=performance.now()+180000,active=()=>available(context,view,s)&&!s.cancelled;
 s.cancel=async()=>{if(s.task?.status==='Running')return await (s.cancelPromise??=context.sceneProxy.invoke('CamSimulation.CancelTask',{epoch:s.task.epoch,taskId:s.task.taskId}));};
 try{
  for(;s.index<s.candidates.length&&active()&&performance.now()<deadline;s.index++){
   s.task=null;s.cancelPromise=null;render();
   s.request??={commandId:crypto.randomUUID(),expected:structuredClone(view.scene.domain.expected),setupId:setupOf(view).id,previewConfiguration:s.candidates[s.index],previewMaximumSeconds:Math.max(1,Math.min(60,(deadline-performance.now())/1000))};
   let task;try{task=await context.sceneProxy.invoke('CamSimulation.PlanMachine',s.request,{timeoutMs:120000});}
   catch(error){if(/CAM_MACHINE_PLACEMENT_|CAM_MACHINE_PREVIEW_PLACEMENT_ONLY/.test(error.message)){s.request=null;continue;}throw error;}
   s.task=task;if(!active())await s.cancel();
   task=await waitForTaskResult(context,task,{active,cancel:s.cancel,onProgress:t=>{s.task=t;const node=context.mount.querySelector('[data-machine-recovery-status]');if(node&&active())node.textContent=`正在检查摆放 ${s.index+1} / ${s.candidates.length} · ${t.machineProgress?.stage==='Solving'?'检查刀路姿态':'检查完整运动'}`;}});
   if(!task)break;s.request=null;s.task=task;
   if(task.status==='Cancelled')break;if(task.status!=='Completed')throw Error(task.error||'摆放检查未完成，请重试。');
   const metadata=await context.sceneProxy.invoke('CamSimulation.ReadMachineAttempt',{epoch:task.epoch,taskId:task.taskId},{timeoutMs:120000});
   const report=await hydrateMachinePlan(metadata,context.sceneProxy,{active});
   if(!report)break;if(report.placementPreview!==true||report.validity!=='Preview')throw Error('摆放检查结果无效，请重新检查。');
   if(report.status==='Ready'&&report.playbackAllowed&&report.collisionStatus==='Clear'){
    s.candidate=report;preview(view,s);break;
   }
  }
  if(active()&&!s.candidate)s.message='本次检查尚未找到通过完整运动检查的位置。可先定位问题，手动平移或旋转工件与工装后重新规划。';
  if(s.cancelled){s.request=null;s.index=0;s.message='检查已取消，原摆放保持不变。';}
 }catch(error){if(active())s.message=error.message;}
 finally{s.running=false;view.fiveMachine.busy=false;render();}
}
