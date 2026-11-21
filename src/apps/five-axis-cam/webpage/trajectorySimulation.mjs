import {chooseCuttingHeadFile} from './cuttingHeadImport.mjs';
import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import { loadRenderResource } from '../../../iCAX-UI/SDK/Viewport/renderResource.mjs';
import { escapeText as esc, escapeAttr as attr } from '../../_shared/workbench/utils/format.mjs';
import { activeTool,selectTool,switchCamPage,camPathKind,switchCamPathKind } from './workspaceModel.mjs';
import { button, tooltipAttributes } from './workspaceControls.mjs';
import { buildPlayback, playbackPose, advancePlayback } from './trajectoryPlayback.mjs';
import { syncCollision, collisionLabel, paintCollision, disposeCollision, jumpCollision, checkCollision,repairCollision } from './simulationCollision.mjs';
import { checkPlaybackContact } from './playbackContact.mjs';
import { paintSimulationTrace, clearSimulationTrace, disposeSimulationTrace } from './simulationTrace.mjs';
import {disposeCollisionProgress,installCollisionProgressStyle,paintCollisionProgress,renderCollisionActivity,renderCollisionIssues,updateCollisionActivity} from './collisionProgress.mjs';
import {collisionDiagnosis,installCollisionDiagnosticsStyle,renderCollisionDiagnosis,renderCollisionRepair,paintCollisionDiagnostics,disposeCollisionDiagnostics,focusCollisionDiagnosis,updateCollisionRepair} from './collisionDiagnostics.mjs';
import {waitForTaskResult} from './taskProgressStream.mjs';
import {saveCommittedProjectEdit} from './projectEditPersistence.mjs';
import {renderHeadWorkspacePreview} from './cuttingHeadWorkspace.mjs';
import {scheduleCollisionThreshold,flushCollisionThreshold} from './collisionThreshold.mjs';
import {refreshMachiningState} from './domainActions.mjs';
import {loadHeadAssembly,paintHeadAssembly,checkedHeadAssemblyPose,rigidHeadDirectionSupported} from './cuttingHeadAssembly.mjs';
import {createSchematicCuttingHead as builtinHead} from './cuttingHeadPreview.mjs';

import {renderHeadInstancePicker,headInstanceCommand} from './cuttingHeadInstances.mjs';
import {camMachinePlayback} from './machinePlaybackContext.mjs';
import {renderCamMachinePlayback,machinePlaybackReady,machineCommand,machineField,stopMachine} from './machineSimulation.mjs';
import {flushHeadColliderSaves} from './cuttingHeadColliders.mjs';
import {collisionPathKey,selectedCollisionPath} from './collisionPathResults.mjs';
import {collisionChecks,selectedCollisionCheck} from './collisionLocations.mjs';
import {repairInOrder,scopeContains,checkLocation} from './collisionRepairSequence.mjs';
import {ensurePlaybackHeadMotion,playbackHeadPose,cancelPlaybackHeadMotion} from './playbackHeadMotion.mjs';
import {headWorkspaceSelection,clearHeadWorkspaceSelection} from './headWorkspaceSelection.mjs';
import {headScopeCommand,headScopeState} from './headAssignmentScopes.mjs';
import {headFitsOperation,headChoiceFitsOperation} from './headMachineCompatibility.mjs';
import {activeSetupWorldFrame,applySetupDisplayFrame} from './setupWorldFrame.mjs';

const setupOf = view => view.scene?.domain?.setups?.find(s => s.id === view.scene.domain.activeSetupId);
const physicalHead = head => {if(!head)return head;const {instance,...definition}=head;return definition;};
const state = view => view.fiveSimulation ??= { cursor:0, speed:1, playing:false, showHead:true, headOpen:false, frames:[], message:'', headError:'' };
const active = (context,view) => !view.disposed && context.isCurrentProject?.() !== false && view.fiveAxisArea === 'toolpath' && activeTool(view) === 'simulation';
const collisionActive = (context,view) => !view.disposed && context.isCurrentProject?.() !== false && view.fiveAxisArea === 'toolpath' && view.fiveCamInspector==='collision' && ['cutting','rapid','order','collision'].includes(view.fiveCamPage||'cutting') && activeTool(view)!=='simulation';
const headActive = (context,view) => !view.disposed && context.isCurrentProject?.() !== false && view.fiveAxisArea === 'toolpath';
function stop(s) { s.playing=false; cancelAnimationFrame(s.animation); s.animation=0; s.lastTime=0; }
function focusCollisionPose(view,s){
  if(focusCollisionDiagnosis(view,s))return;
  const pose=playbackPose(s.frames,s.cursor),viewport=view.viewport;if(!pose||!viewport?.setCameraState)return;
  const position=pose.position.clone().applyMatrix4(activeSetupWorldFrame(view)),camera=viewport.getCameraState();viewport.setCameraState({...camera,target:{x:position.x,y:position.y,z:position.z},radius:Math.min(camera.radius,180)});
}
export function pauseSimulation(view) { if(view.fiveSimulation)stop(view.fiveSimulation);if(view.fiveMachine)stopMachine(view); }
function sync(view) {
  const s=state(view), setup=setupOf(view), geometry=setup?.generatedGeometry;
  const key=JSON.stringify([view.scene?.domain?.expected?.epoch,setup?.id,setup?.assemblyTransform,geometry,view.scene?.domain?.placements,setup?.rapidSettings?.followHeightMm]);
  if (key!==s.sourceKey) {
    stop(s);disposeSimulationTrace(s);s.traceStart=0; s.sourceKey=key; s.cursor=0; s.message='';s.alarmCollision=null;s.frameContact=null;
    try { const result=buildPlayback(setup,view.scene?.domain?.placements??[]); s.frames=result.frames; s.message=view.fiveCamInspector==='collision'&&activeTool(view)!=='simulation'?'':result.reason; }
    catch(error) { s.frames=[]; s.message=error.message; }
    s.collisionKey=null;
  }
  const head=setup?.headModel, headKey=JSON.stringify(physicalHead(head));
  if(headKey!==s.definitionKey) {
    stop(s); s.definitionKey=headKey; s.draft=null; s.headError='';
    s.headKey=null;
  }
  syncCollision(view,s);
  if(view.fiveCamInspector==='collision'&&!s.checking&&!s.repairing&&!s.repairSequence){
    const path=selectedCollisionPath(view),key=JSON.stringify([path,s.collisionKey]);
    if(s.collisionPathSelection!==key){
      s.collisionPathSelection=key;
      if(s.repairScopeSelection!==undefined&&s.repairScopeSelection!==path)s.repairScope=null;
      const segment=(collisionChecks(s.collisionReport)||s.collisionReport?.segments)?.find(segment=>['Collision','Unchecked'].includes(segment.status)&&collisionPathKey(s.frames[Number(segment.index)])===path);
      s.selectedCollisionCheck=segment?.kind?{kind:segment.kind,key:segment.key}:null;
      const index=segment?Number(segment.index):s.frames.findIndex(frame=>collisionPathKey(frame)===path);
      s.cursor=index>=0?index+Math.max(0,Math.min(1-1e-6,Number(segment?.fraction)||0)):0;
      s.collisionPathHasFrame=index>=0;s.collisionDetailXray??=false;
    }
  }
  s.traceContactKey=JSON.stringify([physicalHead(setup?.headModel),setup?.rapidSettings?.collisionThresholdMm,view.scene?.domain?.fixtures,view.scene?.domain?.fixtureUnits,view.scene?.domain?.workpieces]);
  return s;
}
const currentLabel = s => {
  const frame=s.frames[Math.floor(s.cursor)];
  const collision=s.frameContact;
  return frame ? `${frame.order} · ${frame.role==='Rapid'?'空移':frame.role==='LeadIn'?'引入':frame.role==='LeadOut'?'引出':'切割'}${collision?.status==='Collision'?' · 碰撞':collision?.status==='Unchecked'?' · 未验证':''}` : '无轨迹';
};
function headDraft(view) {
  const s=state(view), head=setupOf(view)?.headModel;
  const key=JSON.stringify([setupOf(view)?.id,head]);if(s.draftKey!==key){s.draftKey=key;s.draft=null;}
  return s.draft ??= { tcp:(head?.tcpLocalPosition??[0,0,0]).map(String), beam:(head?.beamLocalDirection??[0,0,-1]).map(String) };
}
export function renderCamHeadCard(view){
 const head=setupOf(view)?.headModel,s=view.fiveSimulation,count=Number(head?.collisionModel?.boxCount)||0;
 const name=head?.instance?.name||(head?.collisionSource==='MachineInstance'?`${head.assembly?.kind?head.assembly.kind+' · ':''}${head.machineHeadName||head.name||'机床切割头'}`:head?.name||'未选择切割头');
 return `<section class="five-cam-head" data-five-key="cam-head"><header><strong>切割头模型</strong>${button('five.simulation.head-open','设置切割头',`aria-expanded="${!!s?.headOpen}" ${view.pending||s?.busy?'disabled':''}`)}</header><div class="five-cam-head-name" title="${attr(name)}">${esc(name)}</div><small>${head?.collisionModel?.status==='MachineBindingRequired'?'刀头结构预览 · 绑定机床后检查完整机构':`${head?.collisionSource==='MachineInstance'?'使用绑定机床 · ':''}${head?.calibrated&&count?`${count} 个喷嘴碰撞盒 · 用于安全空移和碰撞检测`:'请确认切割头及已启用的碰撞盒'}`}</small></section>`;
}
export function renderHeadSettings(view){return view.fiveAxisArea==='toolpath'&&view.fiveCamPage==='head'?renderHeadWorkspacePreview(view):'';}
function collisionHeadName(view,head) {
  if(head?.collisionSource!=='MachineInstance')return head?.name||'当前切割头';
  const machine=view.scene?.machines?.find(item=>(item.entityId||item.id)===head.collisionMachineId);
  return `${machine?.name||machine?.modelName||'绑定机床'}的切割头碰撞盒`;
}
function headCollisionHint(view,head){
 const count=Number(head?.collisionModel?.boxCount)||0;
 return head?.calibrated?`已标定喷嘴端点和光束方向，启用 ${count} 个切割头碰撞盒。`:'请标定喷嘴端点和光束方向，并确认切割头碰撞盒。';
}
export function renderSimulation(view) {
  if(camMachinePlayback(view))return renderCamMachinePlayback(view);
  const s=sync(view), disabled=!s.frames.length||view.pending||view.fiveJobPlayback?.boundary?'disabled':'';
  return `<div class="five-simulation-bar" data-five-key="simulation-bar" data-five-tool="simulation" aria-label="轨迹仿真控制">
    ${button('five.simulation.previous','|◀',`aria-label="上一步" ${tooltipAttributes('跳到上一个轨迹点')} ${disabled} ${s.cursor<=0?'disabled':''}`)}
    ${button('five.simulation.play','▶',`aria-label="播放" ${tooltipAttributes('播放切割头沿切割及空移路径的运动')} ${disabled} ${s.playing?'disabled':''}`)}
    ${button('five.simulation.pause','Ⅱ',`aria-label="暂停" ${tooltipAttributes('暂停播放，保留当前位置')} ${!s.playing?'disabled':''}`)}
    ${button('five.simulation.next','▶|',`aria-label="下一步" ${tooltipAttributes('跳到下一个轨迹点')} ${disabled} ${s.cursor>=s.frames.length-1?'disabled':''}`)}
    <input type="range" min="0" max="${Math.max(0,s.frames.length-1)}" step="any" value="${s.cursor}" data-five-simulation-field="seek" aria-label="仿真进度" ${tooltipAttributes('拖动定位到轨迹中的任意位置')} ${disabled}>
    <span data-five-playback-count>${s.frames.length?Math.floor(s.cursor)+1:0} / ${s.frames.length}</span>
    <select data-five-simulation-field="speed" aria-label="预览速度" ${tooltipAttributes('调整预览播放速度；按路径长度播放，不表示实际加工节拍')}>${[.25,.5,1,2,5,10].map(v=>`<option value="${v}" ${s.speed===v?'selected':''}>${v}×</option>`).join('')}</select>
    <span class="five-playback-divider"></span>
    ${button('five.simulation.head-toggle',s.showHead?'隐藏切割头':'显示切割头',`aria-pressed="${s.showHead}" data-five-head-toggle ${tooltipAttributes(s.showHead?'隐藏视口中的切割头模型':'显示视口中的切割头模型')}`)}
    <details class="five-trace-menu" data-five-key="trace-menu"><summary aria-label="加工轨迹" ${tooltipAttributes('管理仿真播放中留下的加工轨迹')}>加工轨迹 ▾</summary><div role="menu" aria-label="加工轨迹菜单">${button('five.simulation.trace-clear','清除加工轨迹',`role="menuitem" ${tooltipAttributes('清除视口中的加工轨迹显示，保留刀路和模型')}`)}</div></details>
    <span class="five-playback-divider"></span>
    ${Number(s.collisionReport?.collisionCount)?`${button('five.simulation.collision-previous','◀!',`aria-label="上一处碰撞" ${tooltipAttributes('定位到上一处已发现的碰撞')} ${view.pending?'disabled':''}`)}${button('five.simulation.collision-next','!▶',`aria-label="下一处碰撞" ${tooltipAttributes('定位到下一处已发现的碰撞')} ${view.pending?'disabled':''}`)}`:''}
  </div><div class="five-simulation-status" data-five-key="simulation-status"><span data-five-playback-role>${esc(currentLabel(s))}</span><span data-five-collision-status>${esc(collisionLabel(s))}</span><span data-five-playback-message>${esc(s.message)}</span></div>`;
}
export function renderCollisionCheck(view) {
  const s=sync(view),report=s.collisionReport;
  const head=setupOf(view)?.headModel,scope=head?.collisionSource==='MachineInstance'?collisionHeadName(view,head):report?.scope==='BuiltinSchematicWorkpieceAndFixtures'||!head?.geometryRef?'内置示意头':collisionHeadName(view,head);
  const path=selectedCollisionPath(view),groupScope=s.repairScope?.pathKind||(s.checking&&['Cutting','Rapid'].includes(s.checkScope)?s.checkScope:null),belongs=frame=>groupScope?!!frame&&(frame.role==='Rapid')===(groupScope==='Rapid'):collisionPathKey(frame)===path;
  const selected=belongs(s.frames[Math.floor(s.cursor)]),busy=!!(s.checking||s.repairing),details=s.checking?renderCollisionActivity(s):s.repairing?renderCollisionRepair(view,s):selected||groupScope?renderCollisionDiagnosis(view,s)+renderCollisionRepair(view,s):'';
  const globalChecking=s.checking&&s.checkScope==='AllPaths';
  const name=groupScope?`全部${groupScope==='Rapid'?'空移':'切割'}刀路`:globalChecking?'当前装夹全部刀路':camPathKind(view)==='rapid'?'当前空移刀路':'当前切割刀路';
  const pending=s.collisionRequest??s.collisionPreparationRequest;
  const retryPath=!!pending&&(pending.intentId?path===`cut:${pending.intentId}`:pending.to&&path===`rapid:${pending.from}/${pending.to}`);
  return `<section class="five-collision-inspector" data-five-collision-inspector data-five-key="collision-inspector" data-collision-running="${!!s.checking}" role="tabpanel" aria-label="碰撞与超限">
    <header class="five-collision-inspector-heading"><strong>${name}</strong><span>碰撞与超限</span></header>
    <div class="five-collision-dialog-toolbar" data-five-key="collision-toolbar"><span data-five-collision-status>${esc(collisionLabel(s,belongs))}</span><div class="five-collision-dialog-toolbar-actions"><label class="five-collision-threshold">碰撞阈值（mm）<input type="text" inputmode="decimal" aria-label="碰撞阈值" data-five-simulation-field="threshold" data-five-key="collision-threshold" value="${attr(s.thresholdDraft??setupOf(view)?.rapidSettings?.collisionThresholdMm??.1)}" ${view.pending?(view.sceneRefreshPending?'readonly':'disabled'):''}></label>${button(s.checking?'five.simulation.collision-cancel':s.repairing?'five.simulation.repair-cancel':'five.simulation.collision-check-path',s.checking?'取消检测':s.repairing?'取消自动修复':retryPath?'重试本条检测':report?'重新检测本条':'检测本条',!busy&&(!s.collisionPathHasFrame||view.pending)?'disabled':'class="five-primary"')}</div></div>
    <div class="five-collision-inspector-content" data-five-key="collision-content">
      ${renderCollisionIssues(view,s,belongs)||`<div class="five-collision-dialog-empty" data-five-key="collision-empty">${!s.collisionPathHasFrame?'请在左侧选择一条已生成的刀路。':report?.validity==='Current'?'此刀路未发现碰撞或未验证段。':'尚无有效检测结果，请检测本条刀路。'}</div>`}<div class="five-collision-dialog-detail" data-five-key="collision-detail">${details}</div>
    </div>
    ${s.message?`<div class="five-collision-dialog-message" data-five-key="collision-message" role="status">${esc(s.message)}</div>`:''}
    <footer class="five-collision-dialog-footer">检测范围：${name}<br>${scope}与工件、已启用夹具</footer></section>`;
}
function disposeGroup(s) {
  disposeCollisionDiagnostics(s);
  s.loadToken=(s.loadToken||0)+1; s.group?.removeFromParent();
  s.group?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();}); s.group=null; s.laser=null;s.headAssembly=null;
}
export function disposeSimulation(view) { const s=view.fiveSimulation; if(!s)return; stop(s);s.frameContact=null; disposeGroup(s);disposeSimulationTrace(s);disposeCollision(s);disposeCollisionProgress(s);s.headKey=null;void s.cancelCheck?.().catch(()=>{});void s.cancelRepair?.().catch(()=>{}); }
function machineHeadBoxes(head) {
  const result=new THREE.Group();result.name='machine-cutting-head-boxes';
  for(const [index,box] of (head?.collisionModel?.boxes||[]).entries()){
    if(box.enabled===false||box.center?.length!==3||box.halfExtents?.length!==3||box.axes?.length!==3)continue;
    if(!box.halfExtents.every(value=>Number.isFinite(value)&&value>0)||!box.center.every(Number.isFinite)||!box.axes.every(axis=>axis?.length===3&&axis.every(Number.isFinite)))continue;
    const mesh=new THREE.Mesh(new THREE.BoxGeometry(...box.halfExtents.map(value=>value*2)),new THREE.MeshStandardMaterial({color:0x7c8ba2,roughness:.55,metalness:.2}));
    mesh.name='machine-cutting-head-box-'+index;mesh.position.fromArray(box.center);
    mesh.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(...box.axes.map(axis=>new THREE.Vector3(...axis))));mesh.raycast=()=>{};result.add(mesh);
  }
  return result;
}
async function loadHead(context,view,s) {
  const head=setupOf(view)?.headModel, key=JSON.stringify([physicalHead(head),view.viewport?.id,collisionActive(context,view)]);
  if(s.headKey===key&&s.group)return;
  disposeGroup(s);s.headKey=key;s.loadToken??=0;const token=++s.loadToken, viewport=view.viewport;
  const group=new THREE.Group();group.name='five-simulation-head';s.group=group;viewport.scene.add(group);
  const laser=new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(),new THREE.Vector3(0,0,-Number(setupOf(view).rapidSettings.followHeightMm))]),new THREE.LineBasicMaterial({color:0xff3939,depthTest:false}));group.add(laser);s.laser=laser;
  group.traverse(o=>{o.raycast=()=>{};});
  const bound=head?.collisionSource==='MachineInstance',previewRef=bound?head.machinePreviewRef:head?.previewRef;
  if(head?.assembly&&(!collisionActive(context,view)||head.collisionModel?.coverage==='CompleteSelectedHeadAssembly')){
    if(!head.calibrated){s.headError='请先标定工具端的喷嘴端点和光束方向。';return;}
    try{const assembly=await loadHeadAssembly(context.sceneProxy.resources,head.assembly,()=>token===s.loadToken&&s.group===group);
      if(assembly){s.headAssembly=assembly;group.add(assembly);}return;
    }catch(error){if(token===s.loadToken){s.headKey=null;s.headError=error.message;s.message='刀头装配加载失败。';}return;}
  }
  if(!previewRef){group.add(bound?machineHeadBoxes(head):builtinHead());return;}
  if(!head.calibrated)return;
  try {
    const resource=await loadRenderResource(context.sceneProxy.resources,previewRef);
    if(token!==s.loadToken||s.group!==group||!active(context,view)&&!collisionActive(context,view))return;
    const data=resource?.data;if(data?.kind!=='mesh')throw Error('切割头显示模型无效。');
    const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(data.positions,3));
    if(data.indices?.length)geometry.setIndex(Array.from(data.indices));
    if(data.normals?.length===data.positions.length)geometry.setAttribute('normal',new THREE.Float32BufferAttribute(data.normals,3));else geometry.computeVertexNormals();
    const model=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color:0x7c8ba2,roughness:.55,metalness:.2,side:THREE.DoubleSide}));
    model.name=bound?'machine-cutting-head':'imported-cutting-head';model.quaternion.setFromUnitVectors(new THREE.Vector3(...head.beamLocalDirection).normalize(),new THREE.Vector3(0,0,-1));
    model.position.copy(new THREE.Vector3(...head.tcpLocalPosition).applyQuaternion(model.quaternion).negate());model.raycast=()=>{};group.add(model);
  }catch(error){if(token===s.loadToken){s.headKey=null;s.headError=error.message;s.message='切割头加载失败。';if(bound){group.add(machineHeadBoxes(head));s.message='切割头显示模型加载失败，当前显示机床碰撞盒。';}}}
}
function paint(context,view) {
  const s=state(view),mount=context.mount,collision=collisionActive(context,view);
  // A hit at fraction 1 belongs to this record even though its exact pose is
  // the next frame. Keep record selection separate from the displayed pose.
  const diagnosis=collision?collisionDiagnosis(view,s):null;
  const pose=diagnosis?.pose??playbackPose(s.frames,s.cursor);
  const reportedPose=!s.checking||s.checkExactProgress?.current!=null||s.checkExactProgress?.last!=null;
  if(s.group){s.group.visible=!!pose&&(collision||s.showHead)&&reportedPose&&(!collision||s.checking||s.repairing||s.collisionPathHasFrame!==false);if(pose){s.group.position.copy(pose.position);s.group.quaternion.copy(pose.rotation);s.group.userData={cursor:s.cursor,laser:!collision&&pose.laser,tcp:pose.position.toArray(),...(collision?{poseSource:s.checking?'NativeCollisionTaskSegmentStart':s.collisionReport?'CollisionReportLocation':'GeneratedPathPose'}:{})};s.laser.visible=!collision&&pose.laser;}}
  if(s.headAssembly&&pose){const checked=diagnosis?.headPose??playbackHeadPose(s),independent=s.headAssembly.userData.assembly.source!=='MachineInstance';
    if(independent&&!checked)s.headAssembly.visible=false;
    const previous=s.headPoseError,result=independent&&!checked?{valid:false,reason:s.nativeHeadMotion?.error||'正在准备关键点插值姿态…'}:paintHeadAssembly(s.headAssembly,pose,checked);s.headPoseError=result.valid?'':result.reason;s.group.userData.headAssembly={valid:result.valid,source:result.source||'PendingNativeKeyposes',angles:result.angles||{}};if(s.headPoseError)s.message=s.headPoseError;else if(s.message===previous)s.message='';}
  const head=setupOf(view)?.headModel;
  if(s.group&&pose&&head?.kind==='Imported'&&!head.calibrated){s.group.visible=false;s.message='请先标定喷嘴端点和光束方向。';if(s.playing)stop(s);}
  if(s.group&&pose&&head?.motionType==='Rigid'&&!head.assembly&&head.collisionSource!=='MachineInstance'){
    if(!rigidHeadDirectionSupported(pose)){s.group.visible=false;s.laser.visible=false;s.headPoseError='刚性刀头没有旋转轴，不能执行倾斜刀轴；请使用三轴刀路或选择带关节的刀头。';s.message=s.headPoseError;if(s.playing)stop(s);}
    else s.group.quaternion.identity();
  }
  if(s.group&&pose)applySetupDisplayFrame(view,s.group);
  if(pose&&active(context,view)){
    s.frameContact=checkPlaybackContact(view,s);s.alarmCollision=s.frameContact.status==='Collision'?s.frameContact:null;
    if(s.alarmCollision){if(s.playing)stop(s);s.message='当前位置发生碰撞，仿真已暂停。';}
    else if(s.message==='当前位置发生碰撞，仿真已暂停。')s.message='';
    paintSimulationTrace(view,s);
  }
  const text=(selector,value)=>{const el=mount.querySelector(selector);if(el&&el.textContent!==value)el.textContent=value;};
  text('[data-five-playback-count]',`${s.frames.length?Math.floor(s.cursor)+1:0} / ${s.frames.length}`);
  text('[data-five-playback-role]',currentLabel(s));text('[data-five-playback-message]',s.message);
  const message=mount.querySelector('[data-five-playback-message]');if(message){if(/碰撞/.test(s.message))message.setAttribute('role','alert');else message.removeAttribute('role');}
  text('[data-five-collision-status]',collisionLabel(s,collision?frame=>collisionPathKey(frame)===selectedCollisionPath(view):null));paintCollision(view,s);
  if(collision)paintCollisionProgress(view,s);
  if(collision){if(s.collisionPathHasFrame===false&&!s.checking&&!s.repairing)disposeCollisionDiagnostics(s);else paintCollisionDiagnostics(view,s);}
  const slider=mount.querySelector('[data-five-simulation-field="seek"]');if(slider)slider.value=s.cursor;
  for(const [action,disabled] of [['play',s.playing||!s.frames.length||view.pending],['pause',!s.playing],['previous',s.cursor<=0||!s.frames.length||view.pending],['next',s.cursor>=s.frames.length-1||!s.frames.length||view.pending]]) {
    for(const el of [...mount.querySelectorAll(`[data-five-command="five.simulation.${action}"]`),...mount.ownerDocument.querySelectorAll(`.product-ribbon [data-command-id="five.simulation.${action}"]`)])el.disabled=!!disabled||!!view.fiveJobPlayback?.boundary;
  }
}
function animate(context,view) {
  const s=state(view);cancelAnimationFrame(s.animation);
  const tick=time=>{
    if(!s.playing||!active(context,view)||context.mount.ownerDocument.hidden||view.pending){stop(s);if(active(context,view))paint(context,view);return;}
    const seconds=s.lastTime?Math.min((time-s.lastTime)/1000,.1)*s.speed:0;s.lastTime=time;
    if(s.headAssembly?.userData.assembly.source!=='MachineInstance'&&s.headAssembly&&!playbackHeadPose(s)){paint(context,view);s.animation=requestAnimationFrame(tick);return;}
    const result=advancePlayback(s.frames,s.cursor,seconds);s.cursor=result.cursor;s.message=result.reason;
    if(result.stopped)stop(s);paint(context,view);
    if(result.stopped&&!result.reason&&!s.alarmCollision&&s.cursor>=s.frames.length-1)view.fiveJobPlayback?.onEnd?.();
    if(s.playing)s.animation=requestAnimationFrame(tick);
  };
  s.animation=requestAnimationFrame(tick);
}
export function syncSimulation(context,view,ops) {
  if(collisionActive(context,view)&&view.fiveSimulation?.nativeHeadMotion)cancelPlaybackHeadMotion(context,view.fiveSimulation);
  if(camMachinePlayback(view)){if(view.fiveSimulation)cancelPlaybackHeadMotion(context,view.fiveSimulation);disposeSimulation(view);return;}
  if(collisionActive(context,view)){const s=sync(view);stop(s);s.frameContact=null;s.alarmCollision=null;disposeSimulationTrace(s);installCollisionProgressStyle(context.mount?.ownerDocument);installCollisionDiagnosticsStyle(context.mount?.ownerDocument);if(!s.frames.length){disposeGroup(s);s.headKey=null;paintCollision(view,s);return;}void loadHead(context,view,s).then(()=>{if(collisionActive(context,view))paint(context,view);});paint(context,view);return;}
  if(!active(context,view)){if(view.fiveSimulation)cancelPlaybackHeadMotion(context,view.fiveSimulation);disposeSimulation(view);return;}
  const s=sync(view);s.loadToken??=0;
  if(!s.frames.length){disposeGroup(s);s.headKey=null;paint(context,view);return;}
  if(ops)ensurePlaybackHeadMotion(context,view,s,ops,()=>{if(active(context,view))paint(context,view);});
  void loadHead(context,view,s).then(()=>{if(active(context,view))paint(context,view);});paint(context,view);
}
async function previewWorkspaceHead(context,view,choice,ops){
 const selection=headWorkspaceSelection(view),setupId=selection.setupId,serial=++selection.serial;
 const current=()=>!view.disposed&&context.isCurrentProject?.()!==false&&view.fiveHeadSelection===selection&&setupOf(view)?.id===setupId&&selection.serial===serial;
 try{
  await flushHeadColliderSaves(context,view,ops);if(!current())return;
  if(choice===view.fiveHeadEditor?.currentChoice){clearHeadWorkspaceSelection(view);ops.renderProject(context,view);return;}
  Object.assign(selection,{choice,head:null,loading:!!choice,error:'',axis:0});delete selection.draft;
  ops.renderProject(context,view);if(!choice)return;
   if(!headChoiceFitsOperation(view,choice))throw Error('所选切割头不适用于本工序机型。');
  const instanceId=view.fiveHeadEditor?.choices?.get(choice);
  const payload=instanceId?{setupId,kind:'Instance',instanceId}:choice.startsWith('builtin:')?{setupId,kind:'Builtin',builtinDefinitionId:choice.slice(8)}:choice.startsWith('profile:')?{setupId,kind:'Configuration',entryId:choice.slice(8)}:{setupId,kind:'Instance',instanceId:choice};
  const result=await context.sceneProxy.invoke('CamHead.Preview',payload);
   if(current()){if(!result?.head)throw Error('无法读取所选切割头。');if(!headFitsOperation(view,result.head))throw Error('所选切割头不适用于本工序机型。');selection.head=result.head;}
 }catch(error){if(current())selection.error=error.message;}
 finally{if(current()){selection.loading=false;if(view.fiveCamPage==='head')ops.renderProject(context,view);}}
}
export function simulationField(context,view,name,value,ops) {
  if(camMachinePlayback(view)&&['seek','speed'].includes(name)){machineField(context,view,name,value,ops);return;}
  const s=state(view);
  if(name==='head-choice'){
    if(!ops||view.disposed||context.isCurrentProject?.()===false||setupOf(view)?.machineSetup?.machineId||setupOf(view)?.headModel?.collisionSource==='MachineInstance')return;
    if(view.fiveCamPage==='head'&&value!=='import'){
      if(view.pending||s.busy||s.request||view.fiveHeadEditor?.request||view.fiveHeadEditor?.importPrompt||headWorkspaceSelection(view).applying||headScopeState(view).busy||headScopeState(view).request)return;
      void previewWorkspaceHead(context,view,value,ops);return;
    }
    s.resumeHeadChoice=()=>{if(!s.headChoiceBusy&&!view.pending&&s.queuedHeadChoice!==undefined&&!s.request&&!view.fiveHeadEditor?.request)simulationField(context,view,'head-choice',s.queuedHeadChoice,ops);};
    if(s.headChoiceBusy||s.request||view.fiveHeadEditor?.request){s.queuedHeadChoice=value;return;}
    if(view.pending||view.fiveHeadEditor?.importPrompt)return;
    s.headChoiceBusy=true;s.queuedHeadChoice=value;
    void (async()=>{
      try{
        await flushHeadColliderSaves(context,view,ops);
        while(s.queuedHeadChoice!==undefined&&!view.disposed&&context.isCurrentProject?.()!==false){
          const choice=s.queuedHeadChoice;delete s.queuedHeadChoice;s.headChoiceValue=choice;
          if(!headChoiceFitsOperation(view,choice))throw Error('所选切割头不适用于本工序机型。');
          const existing=view.fiveHeadEditor?.choices?.get(choice);
          if(existing){view.fiveHeadEditor.selectedInstance=existing;await headInstanceCommand(context,view,'select',ops);}
          else if(choice==='import')await simulationCommand(context,view,'head-import',ops);
          else if(choice.startsWith('builtin:')){s.builtinHeadIdDraft=choice.slice(8);await simulationCommand(context,view,'head-builtin',ops);}
          else if(choice.startsWith('profile:')){view.fiveHeadEditor.entryId=choice.slice(8);await headInstanceCommand(context,view,'use-profile',ops);}
          else {view.fiveHeadEditor.selectedInstance=choice;await headInstanceCommand(context,view,'select',ops);}
          if(view.fiveHeadEditor?.importPrompt){delete s.queuedHeadChoice;break;}
          if(s.request||view.fiveHeadEditor?.request)break;
        }
      }catch(error){s.headError=error.message;}finally{
        s.headChoiceBusy=false;delete s.headChoiceValue;if(!s.request&&!view.fiveHeadEditor?.request)delete s.queuedHeadChoice;
        if(!view.disposed&&context.isCurrentProject?.()!==false)ops.renderProject(context,view);
      }
    })();return;
  }
  if(name==='seek'&&!view.fiveJobPlayback?.boundary){stop(s);s.alarmCollision=null;s.cursor=Math.max(0,Math.min(s.frames.length-1,Number(value)||0));s.message='';paint(context,view);}
  else if(name==='speed'&&[.25,.5,1,2,5,10].includes(Number(value)))s.speed=Number(value);
  else if(name==='builtin-head'&&setupOf(view)?.headModel?.collisionSource!=='MachineInstance'&&value&&ops&&!view.disposed&&context.isCurrentProject?.()!==false){
    if(view.pending&&!s.busy)return;
    s.builtinHeadIdDraft=value;
    if(s.busy||s.request)s.queuedBuiltinHeadId=value;
    else void simulationCommand(context,view,'head-builtin',ops);
  }
  else if(/^(tcp|beam)[012]$/.test(name)&&setupOf(view)?.headModel?.collisionSource!=='MachineInstance'&&!s.busy&&!s.request)headDraft(view)[name.slice(0,-1)][Number(name.at(-1))]=value;
  else if(name==='threshold'&&!view.pending){s.thresholdDraft=value;if(ops)scheduleCollisionThreshold(context,view,ops);}
  else if(name==='repair-deviation'&&!view.pending)s.repairDeviationDraft=value;
  else if(name==='repair-position'&&!view.pending)s.repairTranslationDraft=value;
}
const parseVector=values=>values.map(v=>{if(!String(v).trim()||!Number.isFinite(Number(v)))throw Error('请输入有效的坐标和方向数值。');return Number(v);});
export async function simulationCommand(context,view,action,ops) {
  if(action==='head-use')return headScopeCommand(context,view,'set',ops);
  const scoped=/^(collision-(?:check|auto-scope|manual-scope)):(Cutting|Rapid)$/.exec(action);
  if(scoped)action=scoped[1];
  if(camMachinePlayback(view)&&['play','pause','previous','next','reset','head-toggle'].includes(action)){
    if(action==='play'&&!machinePlaybackReady(view)){await machineCommand(context,view,'build',ops);if(!machinePlaybackReady(view))return false;}
    return machineCommand(context,view,action,ops);
  }
  if(action==='collision-close'){view.fiveCamInspector='properties';ops.renderProject(context,view);return;}
  const headCommand=action.startsWith('head-'),s=headCommand?state(view):sync(view);
  if(headCommand?!headActive(context,view):!active(context,view)&&!collisionActive(context,view))return;
  if(action==='trace-clear'&&active(context,view)){
    clearSimulationTrace(view,s);const menu=context.mount.querySelector('.five-trace-menu');if(menu){menu.open=false;menu.querySelector('summary').focus({preventScroll:true});}paint(context,view);return;
  }
  if(action==='collision-cancel'){s.repairSequenceCancelled=true;s.collisionCancelRequested=true;updateCollisionActivity(context,s);await s.cancelCheck?.();return;}
  if(action==='repair-cancel'){s.repairSequenceCancelled=true;s.repairCancelRequested=true;updateCollisionRepair(context,s);await s.cancelRepair?.();return;}
  if(action==='head-cancel'){s.headCancelRequested=true;await s.cancelHeadTask?.();return;}
  if(action==='head-close'){s.headOpen=false;switchCamPage(view,'cutting');selectTool(view,'select');ops.renderProject(context,view);return;}
  if(action==='pause'){stop(s);paint(context,view);return;}
  if(view.pending)return;
  if(action==='collision-auto-scope'||action==='collision-manual-scope'){
    const scope={pathKind:scoped?.[2]||(camPathKind(view)==='rapid'?'Rapid':'Cutting')};
    s.repairScope=action==='collision-auto-scope'?scope:null;s.repairScopeSelection=selectedCollisionPath(view);
    s.selectedCollisionCheck=null;
    const first=s.frames.findIndex(frame=>(frame.role==='Rapid')===(scope.pathKind==='Rapid'));s.cursor=Math.max(0,first);
    const issue=collisionChecks(s.collisionReport)?.find(row=>scopeContains(scope,checkLocation(row))&&(row.status==='Collision'||row.reason==='CAM_HEAD_LIMIT_NO_SOLUTION'));
    if(issue){
      const location=checkLocation(issue);
      if(action==='collision-manual-scope'){
        if(view.fiveCamPage==='collision'){switchCamPathKind(view,location.role==='Rapid'?'rapid':'cutting');selectTool(view,location.role==='Rapid'?'rapid':'select');}
        if(location.role==='Rapid'){const {openRapid,rapidPick}=await import('./rapidPlanning.mjs');const pair=location.from+'/'+location.to;openRapid(view,false,false,pair);rapidPick(view,pair);}
        else{const {selectCut}=await import('./cutSelection.mjs');selectCut(view,location.intentId);}
        s.collisionPathSelection=null;sync(view);
      }
      s.selectedCollisionCheck={kind:issue.kind,key:issue.key};s.cursor=Number(issue.index);
    }
    ops.renderProject(context,view);return;
  }
  if(view.fiveJobPlayback?.boundary&&['play','previous','next'].includes(action))return;
  if(action==='collision-check'||action==='collision-check-path'){
    s.repairScope=null;
    let scope={pathKind:scoped?.[2]||(camPathKind(view)==='rapid'?'Rapid':'Cutting')};if(action==='collision-check-path'){
      s.repairScope=null;const path=selectedCollisionPath(view);
      if(path.startsWith('cut:')){const intentId=path.slice(4);if(!intentId)return;scope={intentId};}
      else{const [from,to]=path.slice(6).split('/');if(!from||!to)return;scope={from,to};}
    }
    stop(s);if(!await flushCollisionThreshold(context,view,ops)||!collisionActive(context,view))return;
    if(await checkCollision(context,view,s,ops,()=>collisionActive(context,view),()=>paint(context,view),scope)&&collisionActive(context,view)){sync(view);paint(context,view);ops.renderProject(context,view);}return;
  }
  if(action==='collision-repair-scope'||action==='collision-repair-path'){
    let scope=s.repairScope;
    if(action==='collision-repair-path'){
      const path=selectedCollisionPath(view);if(path.startsWith('cut:'))scope={intentId:path.slice(4)};
      else{const [from,to]=path.slice(6).split('/');scope={from,to};}
    }
    if(!scope)return;
    const angle=s.repairDeviationDraft??String(s.repairDeviationDeg??15),distance=s.repairTranslationDraft??String(s.repairTranslationMm??5);
    if(!String(angle).trim()||!Number.isFinite(Number(angle))||Number(angle)<0||Number(angle)>180||!String(distance).trim()||!Number.isFinite(Number(distance))||Number(distance)<0){s.message='请输入 0～180° 的角度容差和不小于 0 的位置容差。';ops.renderProject(context,view);return;}
    stop(s);s.repairSequence=true;s.repairSequenceCancelled=false;
    const live=()=>collisionActive(context,view),cancelled=()=>!!s.repairSequenceCancelled;
    try{
      const summary=await repairInOrder(scope,{
        readReport:()=>s.collisionReport,active:live,cancelled,
        phaseChanged:phase=>{s.repairSequencePhase=phase;},
        check:async target=>{if(cancelled()||!live())return false;const ok=await checkCollision(context,view,s,ops,live,()=>paint(context,view),target);if(ok&&live())sync(view);return ok;},
        repair:async(issue,repairPhase,toleranceOrigins)=>{s.selectedCollisionCheck={kind:issue.kind,key:issue.key};s.cursor=Number(issue.index);await repairCollision(context,view,s,ops,live,{repairPhase,toleranceOrigins});return s.repairResult;}
      });
      if(live()){s.message=summary.cancelled?'自动修复已取消，已保存的修改可以撤销。':summary.incomplete?summary.reason||s.message||'本次检测未完成，自动修复已停止。':`本次已保存 ${summary.changed} 次局部修正${summary.unresolved.length?`，${summary.unresolved.length} 处仍需调整`:''}。`;sync(view);paint(context,view);}
    }finally{s.repairSequence=false;s.repairScope=null;view.pending=false;if(live())ops.renderProject(context,view);}
    return;
  }
  if(action==='collision-repair'){stop(s);if(await repairCollision(context,view,s,ops,()=>collisionActive(context,view))&&collisionActive(context,view)){sync(view);const focus=s.repairFocus,point=focus?.target&&new THREE.Vector3(...focus.target),index=point?s.frames.findIndex(f=>f.intentId===focus.intentId&&f.role===focus.role&&f.target?.distanceTo(point)<1e-7):-1;s.cursor=index>=0?index:Math.min(s.frames.length-1,focus?.index||0);paint(context,view);ops.renderProject(context,view);}return;}
  if(action==='collision-focus'){focusCollisionPose(view,s);paint(context,view);return;}
  if(action.startsWith('collision-check-result:')){
    const [,kind,key]=action.split(':'),record=collisionChecks(s.collisionReport)?.find(row=>row.kind===kind&&row.key===key);if(!record)return;
    stop(s);s.selectedCollisionCheck={kind,key};s.cursor=Number(record.index)+Math.max(0,Math.min(1,Number(record.fraction)||0));s.collisionDetailXray??=false;focusCollisionPose(view,s);paint(context,view);ops.renderProject(context,view);return;
  }
  if(action.startsWith('collision-edit-point:')){
    const pointId=decodeURIComponent(action.slice('collision-edit-point:'.length)),record=selectedCollisionCheck(s),points=record?.location?[record.location]:[record?.first,record?.last],point=points.find(p=>p?.pointId===pointId);if(!point)return;
    const {toolAxisPointCommand,inspectToolAxisAtPoint}=await import('./toolAxisPoints.mjs');
    await toolAxisPointCommand(context,view,'edit',ops);await inspectToolAxisAtPoint(context,view,point,ops);return;
  }
  if(action.startsWith('collision-edit-rapid:')){
    const frame=action.slice('collision-edit-rapid:'.length),record=selectedCollisionCheck(s),points=record?.location?[record.location]:[record?.first,record?.last],point=points.find(p=>String(p?.frameIndex)===frame);
    if(point?.rapidPointIndex===undefined)return;
    const {rapidCommand,openRapid,rapidPick,flushRapidPathSave}=await import('./rapidPlanning.mjs');const {rapidPathFieldChanged}=await import('./rapidPathEditing.mjs');
    const pair=`${point.from}/${point.to}`;
    if(view.fiveRapid?.id!==pair){if(view.fiveRapid?.pathEdit)await flushRapidPathSave(context,view,ops,true);if(view.fiveCamPage==='collision')switchCamPathKind(view,'rapid');else switchCamPage(view,'rapid');selectTool(view,'rapid');openRapid(view,false,false,pair);rapidPick(view,pair);}
    if(view.fiveRapid?.id!==pair)return;
    await rapidCommand(context,view,'path-edit',ops);const edit=view.fiveRapid?.pathEdit;if(!edit)return;
    const index=Number(point.rapidPointIndex)===Number(point.rapidPointCount)-1?edit.points.length-1:Number(point.rapidPointIndex);
    rapidPathFieldChanged(view,'pathPoint',String(index));edit.poseEditing=false;ops.renderProject(context,view);return;
  }
  if(action==='collision-detail-toggle'){s.collisionDetailXray=!s.collisionDetailXray;paint(context,view);ops.renderProject(context,view);return;}
  if(action.startsWith('collision-edit-anchor:')){
    const frame=action.slice('collision-edit-anchor:'.length),record=selectedCollisionCheck(s),points=record?.location?[record.location]:[record?.first,record?.last],point=points.find(p=>String(p?.frameIndex)===frame);if(!point?.anchor)return;
    const {toolAxisPointCommand,inspectToolAxisAtPoint}=await import('./toolAxisPoints.mjs');
    await toolAxisPointCommand(context,view,'edit',ops);await inspectToolAxisAtPoint(context,view,point,ops);return;
  }
  if(action.startsWith('collision-result:')){const index=Number(action.slice('collision-result:'.length)),segment=s.collisionReport?.segments?.find(x=>Number(x.index)===index&&['Collision','Unchecked'].includes(x.status));if(!segment||!Number.isInteger(index)||!s.frames[index])return;stop(s);s.frameContact=null;s.alarmCollision=null;s.cursor=index+Math.max(0,Math.min(1-1e-6,Number(segment.fraction)||0));s.collisionDetailXray??=false;focusCollisionPose(view,s);paint(context,view);ops.renderProject(context,view);return;}
  if(action==='collision-next'||action==='collision-previous'){stop(s);jumpCollision(s,action==='collision-next');s.collisionDetailXray??=false;focusCollisionPose(view,s);paint(context,view);ops.renderProject(context,view);return;}
  if(action==='threshold-apply'){
    stop(s);await flushCollisionThreshold(context,view,ops);return;
  }
  if(action==='head-open'){
    stop(s);switchCamPage(view,'head');selectTool(view,'select');s.headOpen=true;ops.renderProject(context,view);
    if(s.headOpen&&!view.fiveHeadEditor?.libraryLoaded)void headInstanceCommand(context,view,'refresh-profiles',ops);
    if(s.headOpen&&setupOf(view)?.headModel?.collisionSource!=='MachineInstance'&&!s.builtinHeads&&!s.builtinHeadsLoading){
      s.builtinHeadsLoading=true;ops.renderProject(context,view);
      try{const result=await context.sceneProxy.invoke('CamHead.ListBuiltins',{});if(!view.disposed&&context.isCurrentProject?.()!==false)s.builtinHeads=result.items||[];}
      catch(error){if(!view.disposed&&context.isCurrentProject?.()!==false)s.headError=error.message;}
      finally{s.builtinHeadsLoading=false;if(!view.disposed&&context.isCurrentProject?.()!==false&&(headActive(context,view)))ops.renderProject(context,view);}
    }return;
  }
  if(action==='head-toggle'){s.showHead=!s.showHead;ops.renderProject(context,view);return;}
  if(action==='play'&&s.frames.length){
    if(!active(context,view))return;if(s.cursor>=s.frames.length-1){s.cursor=0;s.traceStart=0;clearSimulationTrace(view,s);}s.message='';s.alarmCollision=null;s.playing=true;s.lastTime=0;
    animate(context,view);paint(context,view);return;}
  if(['previous','next'].includes(action)){stop(s);s.message='';s.cursor=Math.max(0,Math.min(s.frames.length-1,action==='next'?Math.floor(s.cursor)+1:Math.ceil(s.cursor)-1));paint(context,view);
    if(action==='next'&&s.cursor>=s.frames.length-1&&!s.alarmCollision)view.fiveJobPlayback?.onEnd?.();return;}
  if(!['head-import','head-builtin','head-apply'].includes(action))return;
  if(setupOf(view)?.headModel?.collisionSource==='MachineInstance')return;
  stop(s);s.headOpen=true;s.headError='';s.headCancelRequested=false;
  try {
    if(!s.request){
      let sourcePath;const targetSetupId=setupOf(view)?.id;
      if(action==='head-import'){
        sourcePath=await chooseCuttingHeadFile(context,view);
        if(!sourcePath||!headActive(context,view)||setupOf(view)?.id!==targetSetupId)return;
      }
      const domain=view.scene.domain;
      const payload={commandId:crypto.randomUUID(),expected:domain.expected,setupId:domain.activeSetupId};
      if(action==='head-import')payload.sourcePath=sourcePath;
      else if(action==='head-builtin'){if(!s.builtinHeadIdDraft)throw Error('请选择内置切割头。');if(!headChoiceFitsOperation(view,'builtin:'+s.builtinHeadIdDraft))throw Error('所选切割头不适用于本工序机型。');Object.assign(payload,{kind:'Builtin',builtinDefinitionId:s.builtinHeadIdDraft});}
      else {const d=headDraft(view);Object.assign(payload,{kind:'Imported',tcpLocalPosition:parseVector(d.tcp),beamLocalDirection:parseVector(d.beam)});}
      s.request={method:action==='head-import'?'Import':'Configure',payload};
    }
    view.pending=true;s.busy=true;ops.renderProject(context,view);
    const request=s.request;
    let result=await context.sceneProxy.invoke(`CamHead.${request.method}`,request.payload);
    if(request.method==='Import'){
      s.task=result;if(headActive(context,view))ops.renderProject(context,view);
      let cancelPromise;s.cancelHeadTask=()=>cancelPromise??=context.sceneProxy.invoke('CamHead.CancelTask',{epoch:result.epoch,taskId:result.taskId});
      if(s.headCancelRequested)await s.cancelHeadTask();
      result=await waitForTaskResult(context,result,{active:()=>headActive(context,view),cancel:()=>s.cancelHeadTask(),
        queryMethod:'CamHead.QueryTask',cancelMethod:'CamHead.CancelTask',onProgress:progress=>{s.task=progress;}});
      s.request=null;if(!result)return;s.task=result;
      if(result.status==='Cancelled')return;
      if(result.status!=='Completed')throw Error(result.status==='Superseded'?'工程已变化，请重新导入。':result.error||'切割头导入失败。');
    }
    s.request=null;s.draft=null;
    await saveCommittedProjectEdit(context,view,ops);
    if(headActive(context,view)){
      if(!await refreshMachiningState(context,view,ops))throw Error('读取切割头设置失败，请刷新后重试。');
      const imported=setupOf(view)?.headModel;
      if(action==='head-import'&&imported?.instance){clearHeadWorkspaceSelection(view);renderHeadInstancePicker(view);view.fiveHeadEditor.importPrompt={instanceId:imported.instance.id,name:imported.name||imported.instance.name,save:false};}
      ops.appendProjectLog(context,'ok',action==='head-import'?`${imported?.assembly?'刀头机构':'刚性刀头'}已导入${imported?.calibrated?'':'，请标定喷嘴端点和光束方向'}`:'切割头已更新');
    }
  }catch(error){
    const text=error?.message||String(error);
    if(/CAM_|REGISTRATION_/.test(text))s.request=null;
    s.headError=s.request?'操作结果尚未确认，请重试。':text.includes('CAM_HEAD_BEAM_REQUIRED')?'光束方向不能为零。':text.includes('CAM_HEAD_TCP_RANGE')?'喷嘴坐标超出范围。':text.includes('CAM_REVISION_CONFLICT')?'工程已变化，请刷新后重试。':text;
  }finally{
    view.pending=false;s.busy=false;s.cancelHeadTask=null;
    if(headActive(context,view)){
      ops.renderProject(context,view);
      if(!s.request&&s.queuedBuiltinHeadId){const next=s.queuedBuiltinHeadId;s.queuedBuiltinHeadId=null;
        if(next!==setupOf(view)?.headModel?.builtinDefinitionId){s.builtinHeadIdDraft=next;await simulationCommand(context,view,'head-builtin',ops);}
      }
      if(!s.request)s.resumeHeadChoice?.();
    }
  }
}
