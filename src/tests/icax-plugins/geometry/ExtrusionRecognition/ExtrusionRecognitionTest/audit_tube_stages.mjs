import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {stagePlan} from './tube_stage_outputs.mjs';

const root=path.resolve(process.argv[2]??'TestData/Tube'),file=path.join(root,'Reports/summary.json');
const summary=JSON.parse(fs.readFileSync(file,'utf8'));
if(summary.status!=='completed')throw Error('Wait for the batch to complete before auditing.');
const {stageKeys,publishFolders}=stagePlan(summary.until);
summary.counts=Object.fromEntries(['sources','parts',...stageKeys].map(key=>[key,{}]));
const increment=(key,status)=>summary.counts[key][status]=(summary.counts[key][status]??0)+1;
for(const source of summary.sources){
  source.sourceUnchanged=crypto.createHash('sha256').update(fs.readFileSync(source.source)).digest('hex')===source.sha256;
  increment('sources',source.status);
}
for(const p of summary.parts){
  // A native access violation leaves its last checkpoint marked running.
  if(p.status==='running'){
    p.status=p.process?.code!==0?'crashed':'failed';
    p.error=`Native process exited before completing ${p.stage}: ${p.process?.code}`;
    p.stages[p.stage]={status:p.status,error:p.error};
  }
  increment('parts',p.status);
  for(const key of stageKeys)increment(key,p.stages?.[key]?.status??'blocked');
  fs.writeFileSync(p.reportPath,JSON.stringify(p,null,2));
}
const csvCell=v=>`"${String(v??'').replaceAll('"','""')}"`;
const rows=[['source','result',...stageKeys,'machiningFaces','nonMachiningFaces','coloredContour','coloredRoundTrip','error']];
for(const s of summary.sources.filter(s=>s.status!=='passed'))rows.push([s.group,s.status,...stageKeys.map(()=>'blocked'),'','','','',s.error]);
for(const p of summary.parts)rows.push([p.group,p.status,...stageKeys.map(k=>p.stages?.[k]?.status??'blocked'),
  p.stages?.classify?.machiningFaces,p.stages?.classify?.nonMachiningFaces,p.contourOverlay?.exported,p.contourOverlay?.verified,p.error]);
fs.writeFileSync(path.join(root,'Reports/summary.csv'),'\ufeff'+rows.map(r=>r.map(csvCell).join(',')).join('\r\n'));
const audit={createdAt:new Date().toISOString(),sourcesUnchanged:summary.sources.every(s=>s.sourceUnchanged),folders:{},incompleteSteps:[],missingArtifacts:[]};
const publishedRoot=path.resolve(summary.publishRoot);
for(const folder of publishFolders){
  const entries=fs.readdirSync(path.join(publishedRoot,folder),{withFileTypes:true});
  audit.folders[folder]={files:entries.filter(e=>e.isFile()).length,directories:entries.filter(e=>e.isDirectory()).map(e=>e.name)};
  for(const e of entries.filter(e=>e.isFile()&&/\.step$/i.test(e.name))){
    const item=path.join(publishedRoot,folder,e.name),fd=fs.openSync(item,'r'),size=fs.fstatSync(fd).size;
    const tail=Buffer.alloc(Math.min(size,256));fs.readSync(fd,tail,0,tail.length,size-tail.length);fs.closeSync(fd);
    if(!tail.toString('utf8').includes('END-ISO-10303-21;'))audit.incompleteSteps.push(item);
  }
}
for(const p of summary.parts)for(const [kind,item] of Object.entries(p.artifacts??{}))
  if(!fs.existsSync(item)||!fs.statSync(item).isFile())audit.missingArtifacts.push({source:p.group,kind,path:item});
for(const p of summary.parts)if(p.stages?.edgeAnalysis?.status==='passed')
  for(const kind of ['edgeAnalysis','edgeAnalysisData','edgeAnalysisBrep'])
    if(!p.artifacts?.[kind])audit.missingArtifacts.push({source:p.group,kind,reason:'passed edgeAnalysis stage has no published artifact'});
audit.passed=audit.sourcesUnchanged&&!audit.incompleteSteps.length&&!audit.missingArtifacts.length
  &&Object.values(audit.folders).every(f=>!f.directories.length)&&summary.sources.length===summary.sourceCount;
fs.writeFileSync(path.join(root,'Reports/artifact-audit.json'),JSON.stringify(audit,null,2));
summary.artifactAudit=path.join(root,'Reports/artifact-audit.json');fs.writeFileSync(file,JSON.stringify(summary,null,2));
console.log(JSON.stringify({counts:summary.counts,contourOverlays:summary.contourOverlays,audit},null,2));
if(!audit.passed)process.exitCode=1;
