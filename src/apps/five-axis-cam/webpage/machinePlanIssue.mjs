import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {button} from './workspaceControls.mjs';
import {escapeText as esc} from '../../_shared/workbench/utils/format.mjs';
import {floatingDialogStyle} from './floatingDialogs.mjs';
import {registerWorkspaceAlertProvider} from './alerts.mjs';
import {openRapid} from './rapidPlanning.mjs';
import {selectCut} from './cutSelection.mjs';
import {activeTool} from './workspaceModel.mjs';
import {machineForward} from './machineKinematics.mjs';
import {inspectToolAxisAtPoint} from './toolAxisPoints.mjs';

const setupOf=v=>v.scene?.domain?.setups?.find(s=>s.id===v.scene.domain.activeSetupId);
const signature=v=>JSON.stringify([v.scene?.domain?.expected,setupOf(v)?.id,setupOf(v)?.machineSetup,setupOf(v)?.headModel]);
const point=p=>Array.isArray(p)&&p.length===3&&p.every(Number.isFinite);
const witness=r=>r?.rejectedMotion?.collisionJoints?r.rejectedMotion:r?.collisionJoints?r:null;
export function disposeMachinePlanIssue(view){
 const s=view.fiveMachinePlanIssue;if(!s)return;
 s.marker?.removeFromParent();s.marker?.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});s.marker=null;
}
export function presentMachinePlanIssue(view,report){
 disposeMachinePlanIssue(view);
 view.fiveMachinePlanIssue=report?.playbackAllowed===false?{report,signature:signature(view),open:true}:null;
}
export function machinePlanIssue(view){
 const s=view.fiveMachinePlanIssue;
 if(!s||view.disposed||s.signature!==signature(view)){disposeMachinePlanIssue(view);return null;}
 return s;
}
function details(view,s){
 const r=s.report,w=witness(r),t=w?.sourceStation==='Initial'?{}:r.failedTarget||{},cuts=view.scene?.intentToolpaths||view.scene?.domain?.intentToolpaths||[];
 const intentId=t.intentId||w?.sourceIntentId,role=t.role||w?.sourceRole;
 const cut=cuts.find(c=>c.id===intentId),g=setupOf(view)?.generatedGeometry;
 const transition=role==='Rapid'?g?.transitions?.find(t=>t.to===intentId):null;
 const order=id=>g?.operations?.find(o=>o.intentId===id)?.order||cuts.findIndex(c=>c.id===id)+1||'?';
 const label=c=>c?`${c.name||'刀路'}${c.number?`（固定编号 #${c.number}）`:''}`:'未标明刀路';
 const location=transition?`空移 ${transition.fromOrder||order(transition.from)} → ${transition.toOrder||order(transition.to)} · ${label(cuts.find(c=>c.id===transition.from))} → ${label(cut)}`:role==='Rapid'?`进入${label(cut)}的空移`:intentId?`切割 · ${label(cut)}`:'初始姿态／接近运动';
 const collision=r.collisionStatus==='Blocked'&&!!w;
 const title=collision?'候选运动检测到碰撞':r.kinematicsStatus==='Unresolved'&&/逆解|No FK-accepted candidate/.test(r.reason||r.diagnostic||'')?'轴姿态求解未完成':'运动检查未完成';
 const position=t.positionMm||w?.sourcePositionMm,placement=r.configuration?.placement;
 const setupPosition=point(position)&&placement?.length===16?new THREE.Vector3(...position).applyMatrix4(new THREE.Matrix4().set(...placement).invert()).toArray():null;
 return {title,location,collision,position,setupPosition,intentId,role,transition,w,target:t,cut};
}
function endpointName(view,ref){
 const d=view.scene?.domain,p=d?.placements?.find(p=>p.id===ref?.ownerElementId),part=p&&d.workpieces?.find(w=>w.id===p.workpieceId),fixture=d?.fixtures?.find(f=>f.id===ref?.ownerElementId);
 const owner=part?.name||fixture?.name||ref?.ownerElementName||'未命名对象';
 return ref?.boxName&&ref.boxName!==owner?`${owner} · ${ref.boxName}`:owner;
}
export function renderMachinePlanIssue(view){
 const s=machinePlanIssue(view);if(!s?.open||!['toolpath','simulation'].includes(view.fiveAxisArea)||view.fiveMachine?.planning)return '';
 const d=details(view,s),busy=view.pending||view.fiveMachine?.busy?'disabled':'',pairs=d.collision?(d.w.collisionPairs||[]):[];
 const guidance=d.collision?d.role==='Rapid'?'先定位空移段，调整抬升高度、退刀距离或中间路径点，再重新检测。若碰撞发生在连接端点，请调整相邻切割姿态。':'先查看碰撞姿态与相撞对象，再调整刀轴姿态或工件、夹具摆放，重新规划验证。':d.role==='Rapid'?'尚未确认碰撞。可先定位这段空移，检查转姿方向和连接端点；调整后重新规划。':'尚未确认碰撞。可先定位当前检查位置，检查刀轴方向、轴行程和工件摆放后重新规划。';
 const sample=Number(d.target.referenceSample),station=Number(d.target.station),index=d.target.referenceSample!=null&&Number.isSafeInteger(sample)&&sample>=0?`原刀路点 ${sample+1}${d.target.station!=null&&Number.isSafeInteger(station)&&station>=0?` · 关键姿态 ${station+1}`:''}`:'';
 return `<section class="five-plan-issue five-floating-dialog" data-five-floating="machine-plan-issue" ${floatingDialogStyle(view,'machine-plan-issue')} data-five-key="machine-plan-issue" role="dialog" aria-label="运动问题定位"><header data-five-dialog-drag><strong role="alert">${esc(d.title)}</strong>${button('five.plan-issue.close','×','aria-label="关闭运动问题定位"')}</header><div class="five-plan-issue-body" data-five-key="plan-issue-body"><p><strong>${esc(d.location)}</strong></p>${index?`<p>${esc(index)}</p>`:''}${d.setupPosition?`<p>检查目标 TCP · 装夹坐标 · X ${d.setupPosition[0].toFixed(2)} / Y ${d.setupPosition[1].toFixed(2)} / Z ${d.setupPosition[2].toFixed(2)} mm</p>`:''}${pairs.map(p=>`<p class="five-plan-issue-pair">${esc(endpointName(view,p.first))} ↔ ${esc(endpointName(view,p.second))}</p>`).join('')}${d.collision&&!pairs.length?'<p>检测到干涉，但本次未返回完整的碰撞对象定位。</p>':''}<p>${esc(guidance)}</p><p class="five-plan-issue-reason">${esc(s.report.reason||'尚未获得通过验证的运动计划。')}</p><div class="five-plan-issue-actions">${d.setupPosition?button('five.plan-issue.locate','定位问题位置',busy):''}${d.transition||d.intentId?button('five.plan-issue.edit',d.role==='Rapid'?'编辑这段空移':'编辑刀轴姿态',busy):''}${d.w?button('five.plan-issue.inspect','查看碰撞姿态',busy):''}${button('five.plan-issue.replan','重新规划',busy)}</div>${s.message?`<p role="status">${esc(s.message)}</p>`:''}</div></section>`;
}
function paintTarget(view,s,focus=false){
 const d=details(view,s),vp=view.viewport;if(!d.setupPosition||!vp?.scene)return;
 const cam=view.fiveAxisArea==='toolpath'&&activeTool(view)!=='simulation';
 const location=new THREE.Vector3(...(cam?d.setupPosition:d.position));
 // Independent heads also have a virtual work-support chain, with no native
 // scene object for its support ID. Use the same FK frame as playback.
 if(!cam){const inspecting=view.fiveMachine?.inspect&&d.w,q=inspecting?d.w.collisionJoints:s.report.positions;if(!Array.isArray(q)||!s.report.nodes)return;const world=machineForward(s.report,q),support=world[s.report.support];if(!support)return;if(inspecting&&point(s.report.configuration?.tcp)&&world[s.report.tool])location.set(...s.report.configuration.tcp).applyMatrix4(world[s.report.tool]);else location.applyMatrix4(support);}
 if(!s.marker){
  const marker=new THREE.Mesh(new THREE.SphereGeometry(3,16,12),new THREE.MeshBasicMaterial({color:d.collision?0xff4935:0xffbc38,depthTest:false,depthWrite:false}));
  marker.name='MachinePlanIssueTarget';marker.renderOrder=2001;marker.raycast=()=>{};marker.userData={independentInteractionVisual:true,machinePlanIssue:true};s.marker=marker;
 }
 if(s.marker.parent!==vp.scene)vp.scene.add(s.marker);s.marker.position.copy(location);s.marker.updateWorldMatrix(true,false);
 if(focus){const p=s.marker.getWorldPosition(new THREE.Vector3()),camera=vp.getCameraState?.();if(camera)vp.setCameraState({...camera,target:{x:p.x,y:p.y,z:p.z},radius:Math.min(camera.radius,220)});}
 vp.invalidate?.();
}
export function syncMachinePlanIssue(view){
 const s=machinePlanIssue(view);if(!s)return;
 if(!['toolpath','simulation'].includes(view.fiveAxisArea)){disposeMachinePlanIssue(view);return;}
 if(s.located)paintTarget(view,s);
}
export async function machinePlanIssueCommand(context,view,action,ops,navigate){
 const s=machinePlanIssue(view);if(!s)return;
 if(action==='close'){s.open=false;s.located=false;disposeMachinePlanIssue(view);ops.renderProject(context,view);return;}
 if(action==='open'){s.open=true;ops.renderProject(context,view);return;}
 if(view.pending||view.fiveMachine?.busy)return;
 const active=()=>!view.disposed&&context.isCurrentProject?.()!==false&&machinePlanIssue(view)===s;
 const d=details(view,s);
 if(action==='replan'){await navigate('five.machine.build');return;}
 if(action==='inspect'){
  await navigate(s.report.independentHead?'five.area.toolpath':'five.area.simulation');if(!active())return;
  await navigate(s.report.independentHead?'five.tool.simulation':'five.tool.machine-simulation');if(!active())return;
  await ops.refreshActiveAreaView?.(context,view,{});if(!active())return;
  view.fiveMachine.report=s.report;view.fiveMachine.snapshot=s.report;
  await navigate('five.machine.inspect');if(!active())return;
  s.located=true;s.open=false;paintTarget(view,s,true);ops.renderProject(context,view);return;
 }
 if(!['locate','edit'].includes(action))return;
 await navigate('five.area.toolpath');if(!active())return;
 await navigate(`five.cam-page.${d.role==='Rapid'?'rapid':'cutting'}`);if(!active())return;
 await ops.refreshActiveAreaView?.(context,view,{});if(!active())return;
 if(d.transition)openRapid(view,false,false,`${d.transition.from}/${d.transition.to}`);
 else if(d.intentId)selectCut(view,d.intentId);
 // Explicit navigation leaves the viewport and the target editor unobstructed;
 // the same diagnosis remains available from the workspace alert.
 s.located=true;s.open=false;paintTarget(view,s,true);ops.renderProject(context,view);
 if(action==='edit'){
  if(d.transition)await navigate('five.rapid.path-edit');
  else if(d.role!=='Rapid'){
   await navigate('five.tool.keypoint');if(!active())return;
   const p=view.scene?.domain?.placements?.find(p=>p.id===d.cut?.placementId);
   if(p&&d.setupPosition&&point(d.target.beamDirection)){
    const inverse=new THREE.Matrix4().set(...s.report.configuration.placement).invert();
    const direction=new THREE.Vector3(...d.target.beamDirection).transformDirection(inverse);
    const near=new THREE.Vector3(...d.setupPosition).addScaledVector(direction,Number(s.report.nozzleStandOffMm||0)).applyMatrix4(new THREE.Matrix4().set(...p.transform).invert());
    const inspected=await inspectToolAxisAtPoint(context,view,{intentId:d.intentId,nearPoint:near.toArray(),maximumDistanceMm:2},ops);
    if(active()&&!inspected){s.open=true;s.message='已选中对应刀路；未能解析此处刀轴编辑位置，可在场景中点选关键点。';ops.renderProject(context,view);}
   }
  }
  else {s.open=true;s.message='原空移连接已不存在，请重新规划。';ops.renderProject(context,view);}
 }
}
registerWorkspaceAlertProvider('machine-plan-issue',view=>{
 const s=machinePlanIssue(view);if(!s)return [];
 const d=details(view,s);return [{id:'failure',title:d.title,detail:d.location,severity:d.collision?'error':'warning',target:{commandId:'five.plan-issue.open'}}];
});
