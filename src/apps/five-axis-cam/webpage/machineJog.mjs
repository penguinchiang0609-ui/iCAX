import {flushMachinePoseEdits} from './machineIK.mjs';
import {activeTool} from './workspaceModel.mjs';
import {flushMachineColliderSaves} from './machineColliders.mjs';
import {flushMachineParameterEdits} from './machineParameterEdits.mjs';
import {saveEditedProject} from './projectEditPersistence.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {MachineJogPDOStream} from './machineJogPDOStream.mjs';
import {setMachineCollisionFeedback,syncMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature} from './machineCollisionFeedback.mjs';

const selector='[data-five-command^="five.machine.jog-plus:"],[data-five-command^="five.machine.jog-minus:"]';
const reader=view=>view.areas?.[view.activeAreaId]?.viewReader;
function accept(context,view,result){
 const jog=view.fiveMachineJog;if(!jog||result.token!==jog.token)return;
 jog.result=result;
 const descriptor=result.jointIds?result:jog.descriptor;
 if(Array.isArray(result.positions)&&!result.positions.length&&['Invalidated','Error'].includes(result.status)){jog.needsSceneRefresh=true;return;}
 const expected=result.expected||{epoch:result.epoch,mutationSequence:result.mutationSequence},previous=view.scene?.domain?.expected;
 if(expected.epoch===previous?.epoch&&expected.mutationSequence!=null&&BigInt(expected.mutationSequence)>=BigInt(previous.mutationSequence))view.scene.domain.expected={...expected};
 const positions=Array.isArray(result.positions)?Object.fromEntries((descriptor.jointIds||[]).map((id,i)=>[id,result.positions[i]])):result.positions||{};
 if(Array.isArray(result.positions)&&(result.positions.length!==descriptor.jointIds?.length||result.worldMatrices?.length!==descriptor.worldIds?.length*16))throw Error('实时点动回执不完整，已停止。');
 const m=view.scene?.machines?.find(m=>m.entityId===jog.machineId);
 for(const j of m?.joints||[])if(Object.hasOwn(positions,j.entityId)){
  j.position=positions[j.entityId];const field=context.mount.querySelector(`[data-five-machine-field="joint:${j.entityId}"]`);
  if(field&&field!==context.mount.ownerDocument.activeElement)field.value=Number(j.position*(j.type==='prismatic'?1:180/Math.PI)).toFixed(3);
 }
 if(result.worldMatrices?.length&&descriptor.worldIds?.length){
  view.viewport.setTransformPreviews(descriptor.worldIds.map((id,i)=>[(descriptor.worldTransformIds||descriptor.worldIds)[i],new THREE.Matrix4().set(...result.worldMatrices.slice(i*16,i*16+16)).elements]));
 }
 if(result.collisionStatus||Array.isArray(result.collisionPairs)){setMachineCollisionFeedback(view,jog.machineId,result,{geometrySignature:jog.geometrySignature});syncMachineCollisionFeedback(view);}
}
export async function stopMachineJog(view,reason='request'){
 const jog=view?.fiveMachineJog;if(!jog)return;
 if(jog.stopping)return jog.stopping;
 jog.held=false;jog.stopReason=reason;clearTimeout(jog.delay);
 if(!jog.started&&!jog.starting){jog.running=false;return;}
 if(jog.ended)return;
 jog.stopping=(async()=>{
  let saving;
  let transformIds=jog.descriptor?.worldTransformIds||jog.descriptor?.worldIds||[];
  try{
   await jog.starting;
   if(!jog.started)return;
   await jog.stream?.stop({release:true});
   const result=await jog.context.sceneProxy.invoke('CamSimulation.StopMachineJog',{token:jog.token},{timeoutMs:5000});
   jog.ended=true;
   transformIds=[...new Set([...transformIds,...(result.worldTransformIds||result.worldIds||[])])];
   jog.lastStreamStats=jog.stream?{...jog.stream.stats,sequence:String(jog.stream.sequence),writtenSequence:String(jog.stream.writtenSequence),processedSequence:String(jog.stream.processedSequence),resultSequence:String(jog.stream.resultSequence)}:null;
   // A committed stop owns its persistence obligation, even if presentation
   // or history synchronization subsequently fails.
   if(result.changed)saving=saveEditedProject(jog.context).catch(error=>{view.fiveMachine.error='自动保存失败：'+error.message;});
   accept(jog.context,view,result);
   if(result.reason)view.fiveMachine.error=result.reason;
   if(jog.needsSceneRefresh)await jog.ops.refreshSceneState?.(jog.context,view);
   else{await reader(view)?.poll?.();if(jog.context.isCurrentProject?.()!==false&&!view.disposed&&jog.ops.refreshActiveAreaView)await jog.ops.refreshActiveAreaView(jog.context,view,{});}
   await jog.context.actions?.refreshNativeProjectHistory?.({projectId:jog.context.project?.projectId});
  }catch(e){view.fiveMachine.error=e.message;}
  finally{
   await saving;
   // Once Stop is confirmed, a failed View read must not leave an overlay
   // masking later authoritative snapshots or an Undo/Redo restoration.
   if(jog.ended&&!view.disposed)view.viewport?.clearTransformPreviews?.(transformIds);
   jog.running=!!(jog.started&&!jog.ended);if(!jog.ended)jog.stopping=null;
   reader(view)?.setPollInterval?.(100);for(const r of jog.pausedReaders||[])r.setPollingPaused?.(false);
   if(!view.disposed&&jog.context.isCurrentProject?.()!==false){jog.ops.renderProject(jog.context,view);jog.context.actions?.refreshProjectHistoryControls?.();}
  }
 })();return jog.stopping;
}
async function begin(jog,view){
 if(!jog.held)return;
 jog.long=true;jog.ignoreClick=true;
 const {context,ops}=jog,s=view.fiveMachine,m=view.scene?.machines?.find(m=>m.entityId===s.machineId);
 try{
  if(!m||s.busy||view.fiveMachineIK?.inflight||view.fiveMachineIK?.gesture||view.fiveMachineIK?.dragging||view.fiveMachineColliders?.dragging||view.pending||view.disposed||context.isCurrentProject?.()===false)throw Error('机床尚未准备好。');
  await flushMachineParameterEdits(context,view,ops);await flushMachinePoseEdits(context,view,ops);await flushMachineColliderSaves(context,view);if(!jog.held)return;
  const setup=view.scene?.domain?.setups?.find(x=>x.id===view.scene.domain.activeSetupId),bound=setup?.machineSetup?.machineId===m.entityId;
  if(bound&&s.dirty)throw Error('请先应用已修改的工件摆放。');
  const joint=m.joints.find(j=>j.entityId===jog.jointId);
  const speed=Math.min(Number(joint.velocity),Number(s.jogStep||1)*10*(joint.type==='prismatic'?1:Math.PI/180));
  if(!Number.isFinite(speed)||speed<=0)throw Error('该轴未配置有效的速度上限。');
  s.error='';s.report=null;s.snapshot=null;s.snapshotKey=null;jog.running=true;jog.started=true;
  jog.geometrySignature=machineCollisionFeedbackGeometrySignature(view,m.entityId);
  jog.pausedReaders=[...new Set(Object.values(view.areas||{}).map(area=>area.viewReader).filter(r=>r&&r.pollingPaused!==true))];
  for(const r of jog.pausedReaders)r.setPollingPaused?.(true);
  const result=await context.sceneProxy.invoke('CamSimulation.StartMachineJog',{
   token:jog.token,expected:view.scene.domain.expected,...(bound?{setupId:setup.id}:{machineId:m.entityId}),jointId:joint.entityId,direction:jog.direction,speed,
  },{timeoutMs:10000});
  jog.descriptor=result;
  accept(context,view,result);
  if(!jog.held||result.status!=='Running'){void stopMachineJog(view);return;}
  jog.stream=new MachineJogPDOStream(context.sceneProxy.pdo,result,{onResult:status=>{
   if(view.fiveMachineJog!==jog||view.disposed||context.isCurrentProject?.()===false||context.mount.ownerDocument.hidden){void stopMachineJog(view,'inactive');return;}
   accept(context,view,status);if(status.status!=='Running')void stopMachineJog(view,status.status);
  },onError:error=>{s.error=error.message;void stopMachineJog(view,'error');}}).start();
  context.actions?.refreshProjectHistoryControls?.();
 }catch(e){s.error=e.message;jog.held=false;if(jog.started)void stopMachineJog(view);else ops.renderProject(context,view);}
}
export function syncMachineJog(context,view){
 const jog=view.fiveMachineJog;
 if(jog?.held&&(view.fiveAxisArea!=='simulation'||activeTool(view)!==jog.tool||view.fiveMachine?.machineId!==jog.machineId||context.isCurrentProject?.()===false))void stopMachineJog(view);
}
export function consumeMachineJogClick(view,event){
 const jog=view.fiveMachineJog,button=event.target.closest?.(selector);
 if(!jog?.ignoreClick||button?.dataset.fiveCommand!==jog.button.dataset.fiveCommand)return false;
 jog.ignoreClick=false;event.preventDefault();event.stopImmediatePropagation();return true;
}
export function attachMachineJog(mount,binding){
 const doc=mount.ownerDocument,win=doc.defaultView,controller=new AbortController(),options={capture:true,signal:controller.signal};
 let suppressed=null;
 const release=event=>{const jog=binding.view.fiveMachineJog;if(jog?.held||jog?.running&&!jog.ended){if(jog.long)suppressed={button:jog.button,until:performance.now()+1000};void stopMachineJog(binding.view,event?.type||'release');}};
 const press=(event,button,pointerId=null,key=null)=>{
  const {context,view,ops}=binding;if(button.disabled||context.isCurrentProject?.()===false||view.fiveMachine?.busy||view.pending||view.fiveMachineJog?.running)return;
  if(view.fiveMachineJog?.held)return;
  const command=button.dataset.fiveCommand;
  const jog={context,ops,token:crypto.randomUUID(),machineId:view.fiveMachine.machineId,jointId:command.slice(command.indexOf(':')+1),direction:command.includes('jog-minus:')?-1:1,tool:activeTool(view),button,pointerId,key,held:true,started:false,running:false};
  view.fiveMachineJog=jog;suppressed=null;
  jog.delay=setTimeout(()=>{jog.starting=begin(jog,view);},250);
  // Document listeners own release. Capturing to a button would lose the hold
  // when a keyed panel patch moves that same node during the initial delay.
 };
 mount.addEventListener('pointerdown',e=>{const button=e.target.closest?.(selector);if(button&&e.button===0)press(e,button,e.pointerId);},options);
 doc.addEventListener('pointerup',release,options);
 doc.addEventListener('pointercancel',release,options);
 mount.addEventListener('focusout',e=>{const jog=binding.view.fiveMachineJog;if(jog?.key&&e.target===jog.button)release();},options);
 mount.addEventListener('lostpointercapture',release,options);
 doc.addEventListener('pointermove',e=>{
  const jog=binding.view.fiveMachineJog;if(!jog?.held||jog.pointerId!==e.pointerId)return;
  const r=jog.button.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)release();
 },options);
 mount.addEventListener('keydown',e=>{const b=e.target.closest?.(selector);if(!b||![' ','Enter'].includes(e.key))return;e.preventDefault();if(!e.repeat)press(e,b,null,e.key);},options);
 mount.addEventListener('keyup',e=>{const jog=binding.view.fiveMachineJog;if(jog?.held&&jog.key===e.key){e.preventDefault();const short=!jog.long;release();if(short)jog.button.click();}},options);
 mount.addEventListener('click',e=>{
  if(consumeMachineJogClick(binding.view,e))return;
  if(suppressed?.button===e.target.closest?.(selector)&&performance.now()<suppressed.until){e.preventDefault();e.stopImmediatePropagation();suppressed=null;}
  else if(binding.view.fiveMachineJog?.held)release();
 },options);
 doc.addEventListener('visibilitychange',()=>{if(doc.hidden)release();},options);
 doc.addEventListener('keydown',e=>{if(e.key==='Escape')release();},options);
 // Capturing blur also sees an input handing focus to the jog button.
 // Only losing the window itself ends a mouse hold.
 win.addEventListener('blur',event=>{if(event.target===win)release(event);},options);win.addEventListener('pagehide',release,options);
 binding.disposeMachineJog=()=>{release();controller.abort();};
}
