import {collisionChecks} from './collisionLocations.mjs';

export function checkLocation(row){return row.location||row.first;}
export function scopeContains(scope,location){
  if(!location)return false;
  if(scope?.pathKind)return (location.role==='Rapid')===(scope.pathKind==='Rapid');
  return scope?.intentId?location.role!=='Rapid'&&location.intentId===scope.intentId:
    location.role==='Rapid'&&location.from===scope?.from&&location.to===scope?.to;
}
export function locationScope(location){return location.role==='Rapid'?{from:location.from,to:location.to}:{intentId:location.intentId};}
const pointKey=p=>p?.rapidPointIndex!==undefined?[p.from,p.to,p.rapidPointIndex]:[p?.intentId,p?.pointId||p?.anchor?.segmentId,p?.anchor?.parameter];
const issueKey=row=>JSON.stringify([row.kind,pointKey(checkLocation(row)),row.last&&pointKey(row.last)]);
const originKey=p=>JSON.stringify(pointKey(p));
// The UI coordinates existing domain commands. Each command owns candidate
// validation, persistence and history. Explicit repair authorizes these checks;
// ordinary edits never enter this sequence.
export async function repairInOrder(scope,{readReport,check,repair,active,cancelled,phaseChanged=()=>{}}){
  const summary={changed:0,unresolved:[],cancelled:false},origins=new Map();
  if(!await check(scope)){summary.cancelled=cancelled();summary.incomplete=true;return summary;}
  if(readReport()?.validity!=='Current'||!collisionChecks(readReport())){summary.incomplete=true;summary.reason='当前没有有效的关键点检测结果，请手动调整后复检。';return summary;}
  for(const phase of ['Limits','Collision']){
    phaseChanged(phase);const attempted=new Set();
    while(active()&&!cancelled()){
      const rows=(collisionChecks(readReport())||[]).filter(row=>scopeContains(scope,checkLocation(row)));
      const issue=rows.find(row=>(phase==='Limits'?row.reason==='CAM_HEAD_LIMIT_NO_SOLUTION':row.status==='Collision')&&!attempted.has(issueKey(row)));
      if(!issue)break;
      attempted.add(issueKey(issue));
      const result=await repair(issue,phase,[...origins.values()]);
      if(!active()||cancelled()){summary.cancelled=true;return summary;}
      if(!result){summary.unresolved.push(issue);continue;}
      for(const origin of result.toleranceOrigins||[])origins.set(originKey(origin),origin);
      if(!result.changed){summary.unresolved.push(issue);continue;}
      summary.changed++;
      if(!await check(locationScope(checkLocation(issue)))){summary.cancelled=cancelled();summary.incomplete=true;return summary;}
    }
    if(!active()||cancelled()){summary.cancelled=true;return summary;}
  }
  return summary;
}
