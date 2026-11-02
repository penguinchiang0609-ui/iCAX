export function collisionLocationLabel(location){
  if(!location)return '';
  return location.first?`${location.first.label} — ${location.last.label}之间`:location.label||'';
}
export function collisionPathLabel(view,location){
  const point=location?.first||location,cut=view?.scene?.intentToolpaths?.find(c=>c.id===point?.intentId);
  if(point?.role==='Rapid')return `空移 ${point.order??'—'}`;
  return cut?`刀路 #${cut.number} · ${cut.name}`:`刀路 ${point?.order??'—'}`;
}
export function collisionChecks(report){
  if(!Array.isArray(report?.keyposeChecks)||!Array.isArray(report?.interpolationChecks))return null;
  return [...report.keyposeChecks.map(row=>({...row,kind:'keypose',key:String(row.frameIndex)})),...report.interpolationChecks.map(row=>({...row,kind:'interval',key:String(row.firstFrameIndex)}))];
}
export function selectedCollisionCheck(s){
  const selection=s.selectedCollisionCheck;
  return selection?collisionChecks(s.collisionReport)?.find(row=>row.kind===selection.kind&&row.key===selection.key):null;
}
export function collisionIssueLabel(record){
  return record.reason==='CAM_HEAD_LIMIT_NO_SOLUTION'?'限位内无解':record.status==='Collision'?'碰撞':'未验证';
}
export function collisionCounts(report,belongs=()=>true){
  const checks=collisionChecks(report),rows=(checks||report?.segments||[]).filter(belongs);
  return {collisions:rows.filter(r=>r.status==='Collision').length,
    limits:rows.filter(r=>r.reason==='CAM_HEAD_LIMIT_NO_SOLUTION').length,
    unchecked:rows.filter(r=>r.status==='Unchecked'&&r.reason!=='CAM_HEAD_LIMIT_NO_SOLUTION').length,
    skipped:rows.filter(r=>r.status==='Skipped').length,total:rows.length};
}
