import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { publishArtifacts, stagePlan } from './tube_stage_outputs.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const repo=path.resolve(here,'../../../../../..');
const args=process.argv.slice(2);
const supportedOptions=new Set(['--root','--origin','--native','--runtime','--configuration','--publish-root','--workers','--timeout','--max-files','--source-manifest','--heal-merge','--until']);
for(const arg of args)if(arg.startsWith('--')&&!supportedOptions.has(arg))throw Error(`Unsupported option: ${arg}`);
const option=(key,fallback)=>{const i=args.indexOf(key);return i<0?fallback:args[i+1];};
const root=path.resolve(option('--root',path.join(repo,'TestData/Tube')));
const origin=path.resolve(option('--origin',path.join(root,'origin')));
const native=path.resolve(option('--native',path.join(repo,'output/tests/tube-stages/native-debug')));
const publishRoot=path.resolve(option('--publish-root',root));
const until=option('--until','edge-analysis');
const {stageKeys,publishFolders,nativeMode}=stagePlan(until,args.includes('--heal-merge'));
const exe=path.join(native,'TubeStageProbe.exe');
const workers=Number(option('--workers','4')),timeoutMs=Number(option('--timeout','180'))*1000;
const maxFiles=Number(option('--max-files','0'));
const runtime=path.resolve(option('--runtime',path.join(repo,'src/x64/Debug')));
const configuration=option('--configuration','Debug');
if(!['Debug','Release'].includes(configuration))throw Error('--configuration must be Debug or Release');
const env={...process.env,PATH:[native,runtime,path.join(repo,'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),process.env.PATH].join(path.delimiter)};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>{try{return JSON.parse(fs.readFileSync(p,'utf8'));}catch{return null;}};
const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2));};
if(!fs.existsSync(exe))throw Error(`Missing ${exe}`);
const walk=d=>fs.readdirSync(d,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(d,e.name)):[path.join(d,e.name)]);
const sourceManifest=option('--source-manifest','');
let sources=(sourceManifest
  ?JSON.parse(fs.readFileSync(path.resolve(sourceManifest),'utf8')).sources.map(s=>path.resolve(s.source))
  :walk(origin)).filter(p=>/\.(stp|step|igs|iges)$/i.test(p)).sort();
if(sources.some(p=>path.relative(origin,p).startsWith('..')||path.isAbsolute(path.relative(origin,p))))
  throw Error('Selected source is outside the origin directory');
if(maxFiles)sources=sources.slice(0,maxFiles);
const summary={schema:1,startedAt:new Date().toISOString(),status:'running',sourceCount:sources.length,
  workers,timeoutSeconds:timeoutMs/1000,configuration,runtimeRoot:runtime,until,requestedStages:stageKeys,
  artifactFormat:stageKeys.includes('edgeAnalysis')?'STEP; EdgeAnalysis also BREP, JSON and HTML':'STEP',
  timingScope:'computeSeconds is native algorithm time; seconds includes STEP export, read-back and validation; reviewSeconds is the remaining stage time. Interrupted stage seconds are approximate elapsed wall time; unknown computation time stays blank.',
  groundTruth:'Unlabelled real files: passed means invariant checks, not human-certified recognition.',
  materialPreparation:{enabled:false,policy:'Recognize original source faces without solid sewing or union.'},
  heal:{enabled:true,linearToleranceMm:1e-6,mergeSameDomain:args.includes('--heal-merge'),
    policy:'Normalize validated face supports before alignment; optional same-domain merging within each original solid only.'},
  publishRoot,
  assemblyPolicy:'Pass-through ownership: one input model per source; no assembly splitting or connectivity inference. Heal preserves original owners.',
  outputCoordinates:until==='align'?'Heal uses original coordinates. Align applies one rigid transform to +X and centers the model. Transform is recorded per model.'
    :'Heal uses original coordinates. The healed model is then aligned to +X; green outer/red inner contours overlay its minimum-X end. Transform is recorded in the per-model report.',
  runtime:{},sources:[],parts:[]};
for(const name of ['TubeStageProbe.exe','ExtrusionRecognition.dll','OpenCascadeResourceImport.dll','Data.dll','TemplateRuntime.dll']){
  const p=fs.existsSync(path.join(native,name))?path.join(native,name):path.join(runtime,name);
  summary.runtime[name]={path:p,sha256:hash(p)};
}
for(const name of [...publishFolders,'Reports'])fs.mkdirSync(path.join(root,name),{recursive:true});
const pendingParts=[];
function saveSummary(){
  summary.counts=Object.fromEntries(['sources','parts',...stageKeys].map(key=>[key,{}]));
  for(const s of summary.sources)summary.counts.sources[s.status]=(summary.counts.sources[s.status]??0)+1;
  for(const p of summary.parts){
    summary.counts.parts[p.status]=(summary.counts.parts[p.status]??0)+1;
    for(const key of stageKeys){
      const status=p.stages?.[key]?.status??'blocked';summary.counts[key][status]=(summary.counts[key][status]??0)+1;
    }
  }
  write(path.join(root,'Reports/summary.json'),summary);
}
function run(argv,log){
  fs.mkdirSync(path.dirname(log),{recursive:true});const fd=fs.openSync(log,'w');const start=Date.now();
  return new Promise(resolve=>{
    let timedOut=false,settled=false;
    const child=spawn(exe,argv,{cwd:runtime,env,windowsHide:true,stdio:['ignore',fd,fd]});
    const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutMs);
    const finish=(code,error)=>{if(settled)return;settled=true;clearTimeout(timer);fs.closeSync(fd);resolve({code,error,timedOut,seconds:(Date.now()-start)/1000});};
    child.on('error',e=>finish(null,e.message));child.on('close',code=>finish(code));
  });
}
async function pool(items,work){let cursor=0;await Promise.all(Array.from({length:workers},async()=>{while(cursor<items.length){const item=items[cursor++];await work(item);}}));}
await pool(sources,async source=>{
  const group=path.relative(origin,source);
  const directory=path.join(root,'Reports/assembly',group);
  const initialHash=hash(source),runResult=await run(['--split',source,directory],path.join(directory,'split.log'));
  const manifest=read(path.join(directory,'manifest.json'))??{};
  const sourceResult={source,group,sha256:initialHash,sourceUnchanged:hash(source)===initialHash,
    status:runResult.timedOut?'timeout':runResult.code===0&&manifest.status==='passed'?'passed':'failed',
    ...runResult,partCount:manifest.parts?.length??0,assemblyNodes:manifest.assemblyNodes??0,multiSolidLeaves:manifest.multiSolidLeaves??0,
    error:manifest.error??runResult.error,manifest:path.join(directory,'manifest.json')};
  summary.sources.push(sourceResult);
  if(sourceResult.status==='passed')for(const part of manifest.parts)pendingParts.push({source,group,...part});
  else publishArtifacts(publishRoot,path.join(root,'Reports/failed-import'),group,'part-1',sources.map(s=>path.relative(origin,s)),publishFolders);
  saveSummary();console.log(`Import ${summary.sources.length}/${sources.length}: ${group} -> ${sourceResult.status}`);
});
console.log(`Recognizing ${pendingParts.length} unchanged source models`);
await pool(pendingParts,async part=>{
  const staging=path.join(root,'Reports/staging');
  const reportPath=path.join(staging,'Reports',part.group,`${part.id}.json`);
  // Clear only this run's exact report path so a crash cannot reuse an old pass.
  if(fs.existsSync(reportPath))fs.unlinkSync(reportPath);
  for(const [folder,suffix] of [['Reports','-healed.icaxbrep'],['Heal','.step'],['Heal','.brep'],['Heal','.json'],['Align','.step'],['Align','.brep'],['Coutour','.step'],['Coutour','.brep'],['Coutour','.json'],['Coutour','.svg'],['Coutour','.raw.svg'],['Coutour','.solid.raw.svg'],
    ['Classify','-machining.step'],['Classify','-machining.brep'],['Classify','-non-machining.step'],['Classify','-non-machining.brep'],['Classify','-split.step'],['Classify','-split.brep'],
    ['EdgeAnalysis','.step'],['EdgeAnalysis','.json'],['EdgeAnalysis','.brep'],['EdgeAnalysis','.html']]){
    if(folder!=='Reports'&&!publishFolders.includes(folder))continue;
    const previous=path.join(staging,folder,part.group,part.id+suffix);
    if(fs.existsSync(previous))fs.unlinkSync(previous);
  }
  const runResult=await run([nativeMode,part.brep,staging,part.group,part.id],path.join(root,'Reports',part.group,`${part.id}.log`));
  const report=read(reportPath)??{status:'failed',error:'Native process did not write a report',stages:{}};
  const interruptedStage=(status,error)=>{
    const stage=report.stage??'import';report.stages??={};
    const previous=report.stages[stage]??{};
    report.stages[stage]={...previous,status,error,
      seconds:Math.max(0,runResult.seconds-(previous.startedSeconds??0)),secondsApproximate:true};
  };
  if(runResult.timedOut){report.status='timeout';report.error=`Process exceeded ${timeoutMs/1000} seconds during ${report.stage??'import'}`;interruptedStage('timeout',report.error);}
  else if(report.status==='running'){
    report.status=runResult.code!==0?'crashed':'failed';
    report.error=`Native process exited before completing ${report.stage}: ${runResult.code}`;
    interruptedStage(report.status,report.error);
  }
  const result={...part,...report,process:runResult,reportPath};
  result.artifacts=publishArtifacts(publishRoot,staging,part.group,part.id,summary.sources.map(s=>s.group),publishFolders);
  if(runResult.code!==0&&result.status==='passed')result.status='failed';
  if(result.status==='passed'){
    const incomplete=stageKeys.filter(key=>result.stages?.[key]?.status!=='passed');
    if(incomplete.length){result.status='failed';result.error=`Native report did not pass all requested stages: ${incomplete.join(', ')}`;}
  }
  summary.parts.push(result);write(reportPath,result);saveSummary();
  console.log(`Part ${summary.parts.length}/${pendingParts.length}: ${part.group}/${part.id} -> ${result.status} (${report.stage})`);
});
summary.status='completed';summary.completedAt=new Date().toISOString();summary.partCount=pendingParts.length;
summary.sources.sort((a,b)=>a.group.localeCompare(b.group));summary.parts.sort((a,b)=>a.group.localeCompare(b.group)||a.id.localeCompare(b.id));
for(const s of summary.sources)s.sourceUnchanged=hash(s.source)===s.sha256;
saveSummary();
const csvCell=v=>`"${String(v??'').replaceAll('"','""')}"`;
const timingHeaders=stageKeys.flatMap(stage=>[`${stage}ComputeSeconds`,`${stage}ReviewSeconds`,`${stage}TotalSeconds`]);
const rows=[['source','part','name','assemblyPath','result',...stageKeys,...timingHeaders,'machiningFaces','nonMachiningFaces','processSeconds','sourceImportSeconds','error']];
const timings=[['source','part','stage','status','computeSeconds','reviewSeconds','totalSeconds','totalSecondsApproximate','error']];
for(const s of summary.sources.filter(s=>s.status!=='passed')){
  rows.push([s.group,'','','',s.status,...stageKeys.map(()=>'blocked'),...timingHeaders.map(()=>''),'','',s.seconds,s.seconds,s.error]);
  for(const stage of stageKeys)timings.push([s.group,'',stage,'blocked','','','','',s.error]);
}
for(const p of summary.parts){
  const source=summary.sources.find(source=>source.group===p.group);
  rows.push([p.group,p.id,p.name,p.assemblyPath,p.status,...stageKeys.map(s=>p.stages?.[s]?.status??'blocked'),
    ...stageKeys.flatMap(stage=>[p.stages?.[stage]?.computeSeconds,p.stages?.[stage]?.reviewSeconds,p.stages?.[stage]?.seconds]),
    p.stages?.classify?.machiningFaces,p.stages?.classify?.nonMachiningFaces,p.process.seconds,source?.seconds,p.error]);
  for(const stage of stageKeys){const t=p.stages?.[stage];timings.push([p.group,p.id,stage,t?.status??'blocked',t?.computeSeconds,t?.reviewSeconds,t?.seconds,t?.secondsApproximate??false,t?.error??(t?.status==='passed'?'':p.error)]);}
}
fs.writeFileSync(path.join(root,'Reports/summary.csv'),'\ufeff'+rows.map(row=>row.map(csvCell).join(',')).join('\r\n'));
fs.writeFileSync(path.join(root,'Reports/stage-timings.csv'),'\ufeff'+timings.map(row=>row.map(csvCell).join(',')).join('\r\n'));
summary.timingTotals=Object.fromEntries(stageKeys.map(stage=>{
  const completed=summary.parts.map(p=>p.stages?.[stage]).filter(t=>Number.isFinite(t?.seconds)&&!t.secondsApproximate);
  const computed=completed.filter(t=>Number.isFinite(t.computeSeconds));
  return [stage,{measuredStages:completed.length,measuredComputations:computed.length,
    computeSeconds:computed.reduce((sum,t)=>sum+t.computeSeconds,0),
    reviewSeconds:computed.reduce((sum,t)=>sum+(t.reviewSeconds??0),0),
    totalStageSeconds:completed.reduce((sum,t)=>sum+t.seconds,0),
    maxStageSeconds:completed.length?Math.max(...completed.map(t=>t.seconds)):null}];
}));
saveSummary();
console.log(JSON.stringify(summary.counts,null,2));
