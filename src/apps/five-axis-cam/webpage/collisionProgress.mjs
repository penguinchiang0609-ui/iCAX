import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {applySetupDisplayFrame} from './setupWorldFrame.mjs';
import {escapeText as esc,escapeAttr as attr} from '../../_shared/workbench/utils/format.mjs';
import {collisionObstacle} from './collisionDiagnostics.mjs';
import {collisionChecks,collisionLocationLabel,collisionPathLabel,collisionIssueLabel} from './collisionLocations.mjs';

const integer=v=>String(v??'').trim()!==''&&Number.isSafeInteger(Number(v))&&Number(v)>=0?Number(v):null;
const role=f=>f?.role==='Rapid'?'空移':f?.role==='LeadIn'?'引入':f?.role==='LeadOut'?'引出':'切割';
export function installCollisionProgressStyle(document){
  if(!document?.head||document.getElementById('five-collision-progress-style'))return;
  const link=document.createElement('link');link.id='five-collision-progress-style';link.rel='stylesheet';link.href=new URL('./collisionProgress.css',import.meta.url).href;document.head.append(link);
}

// Segment indices are native task facts, never estimates from percentages.
// The task maps retry-local progress back to the original full trajectory.
export function acceptCollisionProgress(s,task){
  const percent=Number(task?.progress);if(Number.isFinite(percent))s.checkProgress=Math.max(0,Math.min(100,percent));
  const p=task?.collisionProgress;if(!p){s.checkExactProgress=null;return;}
  const completed=integer(p.completedSegmentCount),total=integer(p.totalSegmentCount),full=integer(p.fullSegmentCount);
  if(completed===null||total===null||full===null||!total||completed>total||total>full||full!==Math.max(1,(s.frames?.length??0)-1)){s.checkExactProgress=null;return;}
  const inRange=index=>index!==null&&index<full&&index<s.frames.length;
  const current=integer(p.currentSegmentIndex),last=integer(p.lastCompletedSegmentIndex);
  s.checkExactProgress={completed,total,full,current:inRange(current)&&completed<total&&task.status==='Running'?current:null,last:inRange(last)&&completed>0?last:null};
  s.checkExactProgress.location=p.location;
  const workers=integer(p.workerCount),active=integer(p.activeTaskCount);if(workers>1&&active!==null&&active<=workers)s.checkExactProgress.parallel={workers,active};
  if(p.stage==='Keypoints'){const done=integer(p.completedKeypointCount),count=integer(p.totalKeypointCount);if(done!==null&&count!==null&&count>0&&done<=count)s.checkExactProgress.keypoints={done,total:count};}
  const cursor=s.checkExactProgress.current??s.checkExactProgress.last;
  if(cursor!==null)s.cursor=cursor;
}
export function collisionActivity(s){
  const p=s.checkExactProgress,current=p?.current,frame=current!==null&&current!==undefined?s.frames[current]:null;
  const stage=s.collisionCancelRequested?'正在停止检测':s.checkStage==='publishing'?'正在保存检测结果':p?.keypoints?'先检查关键点姿态':s.checkStage==='checking'?'正在检查切割头避让':'正在准备检测';
  const detail=s.collisionCancelRequested?'当前运算结束后停止，已保存的工程保持可用。':s.checkStage==='publishing'?'计算已完成，正在保存并更新碰撞结果。':p?.location?`${collisionPathLabel(null,p.location)} · ${collisionLocationLabel(p.location)}`:frame?`刀路 ${frame.order} · ${role(frame)}`:s.checkStage==='checking'?'正在检查切割头与工件、夹具之间的间隙。':'读取当前切割、空移、切割头与装夹。';
  const count=(p?.keypoints?`已检查 ${p.keypoints.done} / ${p.keypoints.total} 个关键点，随后检查点间连续运动`:p?`本次已检查 ${p.completed} / ${p.total} 个关键点区间`:'准备完成后显示实际检测进度')+(p?.parallel?` · 同时检测 ${p.parallel.active} 处`:'');
  const pose=frame?(p?.parallel?'三维中显示正在并行检测的一段运动。':'三维中显示当前检测段的起点姿态与运动范围。'):'右键旋转 · 中键平移 · 滚轮缩放';
  return{stage,detail,count,pose,percent:Math.max(0,Math.min(100,Number(s.checkProgress)||0)),hasExact:!!p};
}
export function renderCollisionActivity(s){
  const a=collisionActivity(s);
  return `<section class="five-collision-activity" data-five-key="collision-activity" aria-live="polite"><div class="five-collision-stage"><span class="five-collision-scanner" aria-hidden="true"><svg viewBox="0 0 48 48"><path class="body" d="M18 5h12v13l-4 8h-4l-4-8z"/><path class="beam" d="M24 27v10M16 37h16"/><path class="part" d="M7 40h34M11 44h26"/></svg></span><div><strong data-five-collision-stage>${esc(a.stage)}</strong><p data-five-collision-detail>${esc(a.detail)}</p></div></div><div class="five-collision-progress" role="progressbar" aria-label="碰撞检测进度" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${a.percent}"><span data-five-collision-fill style="width:${a.percent}%"></span></div><div class="five-collision-progress-caption"><span data-five-collision-count>${esc(a.count)}</span><strong data-five-collision-percent>${a.percent}%</strong></div><p class="five-collision-pose-hint" data-five-collision-pose>${esc(a.pose)}</p></section>`;
}
export function updateCollisionActivity(context,s){
  const root=context.mount;if(!root?.querySelector)return;const a=collisionActivity(s),text=(selector,value)=>{const e=root.querySelector(selector);if(e&&e.textContent!==value)e.textContent=value;};
  text('[data-five-collision-stage]',a.stage);text('[data-five-collision-detail]',a.detail);text('[data-five-collision-count]',a.count);text('[data-five-collision-percent]',`${a.percent}%`);text('[data-five-collision-pose]',a.pose);
  const bar=root.querySelector('[role="progressbar"][aria-label="碰撞检测进度"]');bar?.setAttribute('aria-valuenow',String(a.percent));const fill=root.querySelector('[data-five-collision-fill]');if(fill)fill.style.width=`${a.percent}%`;
}
export function disposeCollisionProgress(s){
  if(s.checkAnimation&&typeof cancelAnimationFrame==='function')cancelAnimationFrame(s.checkAnimation);s.checkAnimation=0;
  const p=s.checkOverlay;p?.group.removeFromParent();p?.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});s.checkOverlay=null;
}
export function paintCollisionProgress(view,s){
  const index=s.checkExactProgress?.current,frame=index!==null&&index!==undefined?s.frames[index]:null,next=frame?s.frames[index+1]:null;
  if(!s.checking||!frame||!view.viewport){if(s.checkOverlay)s.checkOverlay.group.visible=false;return;}
  if(s.checkOverlay?.index===index){s.checkOverlay.group.visible=true;applySetupDisplayFrame(view,s.checkOverlay.group);return;}
  const old=s.checkOverlay;old?.group.removeFromParent();old?.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});
  const group=new THREE.Group();group.name='five-collision-active-segment';group.userData={segmentIndex:index,poseSource:'NativeCollisionTaskSegmentStart'};
  const material=new THREE.LineBasicMaterial({color:0x39c9cf,depthTest:false,transparent:true,opacity:.9});
  // A discontinuous connector has no swept geometry to preview.
  if(next&&!next.breakBefore){const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints([frame.position,next.position]),material);line.name='collision-current-sweep';line.renderOrder=95;line.raycast=()=>{};group.add(line);}else material.dispose();
  const marker=new THREE.Mesh(new THREE.SphereGeometry(1.6,12,8),new THREE.MeshBasicMaterial({color:0x39c9cf,depthTest:false,transparent:true,opacity:.9}));marker.name='collision-current-start';marker.position.copy(frame.position);marker.renderOrder=96;marker.raycast=()=>{};group.add(marker);view.viewport.scene.add(group);s.checkOverlay={group,index};
  applySetupDisplayFrame(view,group);
  marker.onBeforeRender=(_renderer,_scene,camera)=>{const height=Math.max(1,view.viewport.renderer.domElement.clientHeight),world=camera.isOrthographicCamera?(camera.top-camera.bottom)/camera.zoom:2*marker.getWorldPosition(new THREE.Vector3()).distanceTo(camera.position)*Math.tan(THREE.MathUtils.degToRad(camera.fov/2));marker.scale.setScalar(world/height*5/1.6);marker.updateMatrixWorld();};
}
export function animateCollisionProgress(context,view,s,active,onPose){
  disposeCollisionProgress(s);installCollisionProgressStyle(context.mount?.ownerDocument);
  if(typeof requestAnimationFrame!=='function')return;
  const reduced=context.mount?.ownerDocument?.defaultView?.matchMedia?.('(prefers-reduced-motion: reduce)').matches;
  const tick=time=>{if(!s.checking||!active()){disposeCollisionProgress(s);return;}paintCollisionProgress(view,s);const overlay=s.checkOverlay;if(overlay)overlay.group.traverse(o=>{if(o.material?.transparent)o.material.opacity=reduced ? .85 : .6+.3*(.5+.5*Math.sin(time/230));});onPose?.();s.checkAnimation=requestAnimationFrame(tick);};
  s.checkAnimation=requestAnimationFrame(tick);
}
export function collisionIssues(s){
  const checks=collisionChecks(s.collisionReport);
  if(checks)return checks.filter(row=>['Collision','Unchecked'].includes(row.status)).map(row=>({index:Number(row.index),last:Number(row.index),count:1,status:row.status,reason:row.reason,obstacleId:row.obstacleId,boxIndex:integer(row.witness?.headBoxIndex),kind:row.kind,key:row.key,location:row.location||{first:row.first,last:row.last}}));
  const groups=[];for(const segment of s.collisionReport?.segments??[]){if(!['Collision','Unchecked'].includes(segment.status))continue;const index=integer(segment.index);if(index===null)continue;const frame=s.frames[index],obstacleId=segment.obstacleId||'',boxIndex=integer(segment.witness?.headBoxIndex),prior=groups.at(-1);if(prior&&prior.status===segment.status&&index===prior.last+1&&prior.obstacleId===obstacleId&&prior.boxIndex===boxIndex&&prior.order===frame?.order&&prior.role===role(frame)){prior.last=index;prior.count++;continue;}groups.push({index,last:index,count:1,status:segment.status,order:frame?.order,role:role(frame),reason:segment.reason||'',obstacleId,boxIndex});}return groups;
}
export function renderCollisionIssues(view,s,accept=()=>true){
  const issues=collisionIssues(s).filter(issue=>accept(s.frames[issue.index]));if(!issues.length)return '';
  return `<section class="five-collision-issues" data-five-key="collision-issues"><h4>检测结果 <span>${issues.length} 处</span></h4><div class="five-collision-issue-list" data-five-key="collision-issue-list" aria-label="碰撞与未验证列表">${issues.map(issue=>{
    const obstacle=collisionObstacle(view,issue.obstacleId),label=collisionIssueLabel(issue),selected=issue.kind?s.selectedCollisionCheck?.kind===issue.kind&&s.selectedCollisionCheck?.key===issue.key:Math.floor(s.cursor)>=issue.index&&Math.floor(s.cursor)<=issue.last;
    const location=issue.location||s.frames[issue.index],command=issue.kind?`collision-check-result:${issue.kind}:${issue.key}`:`collision-result:${issue.index}`;
    return `<button type="button" class="five-collision-issue ${issue.status==='Collision'?'is-collision':'is-unchecked'}" data-five-key="collision-issue-${issue.kind||'sample'}-${issue.key||issue.index}" aria-pressed="${selected}" data-five-command="five.simulation.${command}" ${view.pending?'disabled':''} title="${attr(obstacle.name||issue.reason||label)}"><span><b>${label}</b> · ${esc(collisionPathLabel(view,location))}</span><small>${esc(collisionLocationLabel(issue.location)||issue.role)}${issue.obstacleId?` · ${esc(obstacle.kind)} ${esc(obstacle.name)}`:''}${issue.boxIndex!==null?` · 盒 ${issue.boxIndex+1}`:''}</small><span class="five-collision-locate" aria-hidden="true">定位 ›</span></button>`;
  }).join('')}</div></section>`;
}
