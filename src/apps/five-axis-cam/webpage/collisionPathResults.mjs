import {buildPlayback} from './trajectoryPlayback.mjs';
import {workspaceState,camPathKind} from './workspaceModel.mjs';

const cache=new WeakMap();
export function retainUnaffectedCollisions(setup,ids){
  const report=setup?.simulationCheck,affected=new Set(ids),ops=setup?.generatedGeometry?.operations||[],previous=new Map(ops.slice(1).map((op,i)=>[op.intentId,ops[i].intentId]));
  if(ops.length)setup.generatedGeometry.partialDisplay=true;
  if(report?.validity!=='Current'||!report.frames||!report.segments)return false;
  const changed=frame=>frame&&(affected.has(frame.intentId)||frame.role==='Rapid'&&affected.has(previous.get(frame.intentId)));
  report.segments=report.segments.map(segment=>changed(report.frames[Number(segment.index)])||changed(report.frames[Number(segment.index)+1])?{index:segment.index,status:'Unchecked',fraction:0,reason:'CAM_COLLISION_LOCAL_EDIT',certifiedClearanceMm:0}:segment);
  report.collisionCount=String(report.segments.filter(s=>s.status==='Collision').length);report.uncheckedCount=String(report.segments.filter(s=>s.status==='Unchecked').length);
  report.status=Number(report.collisionCount)?'Collision':Number(report.uncheckedCount)?'Unchecked':'Clear';report.keypointResultsCurrent=false;
  setup.generatedGeometry.partialDisplay=true;return true;
}
export const collisionPathKey=frame=>frame?.role==='Rapid'?`rapid:${frame.from}/${frame.to}`:frame?.intentId?`cut:${frame.intentId}`:'';
export function selectedCollisionPath(view){
  return camPathKind(view)==='rapid'?`rapid:${view.fiveRapid?.id||view.fiveRapidSelectedId||''}`:`cut:${workspaceState(view).selectedIntentId||''}`;
}
// A report must still describe the exact generated trajectory before it can
// mark either list. Rapid ownership is the connection pair, not its next cut.
export function collisionPathResults(view){
  const domain=view.scene?.domain,setup=domain?.setups?.find(s=>s.id===domain.activeSetupId),report=setup?.simulationCheck;
  const key=JSON.stringify([report,setup?.generatedGeometry,domain?.placements,setup?.rapidSettings?.followHeightMm]);
  const previous=cache.get(view);if(previous?.key===key)return previous;
  const result={key,report:null,frames:[],paths:new Map()};cache.set(view,result);
  if(report?.validity!=='Current')return result;
  try{result.frames=buildPlayback(setup,domain.placements||[]).frames;}catch{return result;}
  if(!result.frames.length||report.frames?.length!==result.frames.length||report.frames.some((f,i)=>{
    const frame=result.frames[i];return !Array.isArray(f.position)||!Array.isArray(f.rotation)||f.position.length!==3||f.rotation.length!==4||
      !f.position.every(Number.isFinite)||Math.hypot(...f.position.map((n,j)=>n-frame.position.getComponent(j)))>1e-6||
      !f.rotation.every(Number.isFinite)||frame.rotation.angleTo({x:f.rotation[0],y:f.rotation[1],z:f.rotation[2],w:f.rotation[3]})>1e-6;
  }))return result;
  result.report=report;
  for(const segment of report.segments||[]){
    const index=Number(segment.index);if(!['Collision','Unchecked'].includes(segment.status)||!Number.isInteger(index)||index<0||!result.frames[index])continue;
    const path=collisionPathKey(result.frames[index]);if(!path||collisionPathKey(result.frames[index+1])!==path)continue;
    if(!result.paths.has(path))result.paths.set(path,[]);result.paths.get(path).push(segment);
  }
  return result;
}
export function collisionPathWarning(results,path){
  const segments=results.paths.get(path);if(!segments?.length)return '';
  const count=segments.filter(s=>s.status==='Collision').length,label=count?`有碰撞：${count} 段${segments.length>count?'，另有未验证段':''}`:`有 ${segments.length} 段未验证`;
  return `<span class="five-path-collision-warning" data-five-collision-warning role="img" aria-label="${label}" title="${label}；选中刀路后在右侧碰撞页查看"><svg width="16" height="16" viewBox="0 0 20 20" aria-hidden="true"><path d="M10 2 19 18H1Z" fill="#f4bf37" stroke="#9b6c0a"/><path d="M10 7v5m0 2v1" stroke="#523b0a" stroke-width="1.8"/></svg></span>`;
}
export function collisionPathStatus(results,path){
  if(!results.report)return '';
  const issues=results.paths.get(path)||[];
  if(issues.some(row=>row.status==='Collision'))return '检测到碰撞';
  if(issues.some(row=>row.reason==='CAM_HEAD_LIMIT_NO_SOLUTION'))return '超出切割头限位';
  if(issues.length)return '';
  const indices=results.frames.flatMap((frame,index)=>index+1<results.frames.length&&collisionPathKey(frame)===path&&collisionPathKey(results.frames[index+1])===path?[index]:[]);
  const rows=new Map(results.report.segments.map(row=>[Number(row.index),row]));
  return indices.length&&indices.every(index=>rows.get(index)?.status==='Clear')?'碰撞与限位检测通过':'';
}
export function collisionPathBadge(results,path){
  const status=collisionPathStatus(results,path);
  if(status==='碰撞与限位检测通过')return '<span class="five-path-collision-pass" data-five-collision-pass role="img" aria-label="碰撞与超限检测通过" title="碰撞与超限检测通过"><svg width="16" height="16" viewBox="0 0 20 20" aria-hidden="true"><circle cx="10" cy="10" r="8" fill="#2c9b57"/><path d="m5.5 10 3 3 6-6" fill="none" stroke="white" stroke-width="2"/></svg></span>';
  return collisionPathWarning(results,path)||'<span class="five-path-unchecked" title="尚无有效检测结果">未检测</span>';
}
