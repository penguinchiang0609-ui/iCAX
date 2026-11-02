import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {publishArtifacts} from '../../tests/icax-plugins/geometry/ExtrusionRecognition/ExtrusionRecognitionTest/tube_stage_outputs.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2));};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const extensions=['.step','.json','.brep','.html'];
const stageBase=(staging,part)=>path.join(staging,'EdgeAnalysis',part.group,part.id);

export function validatePublishDirectory(publish) {
  if(path.basename(publish)!=='EdgeAnalysis')throw Error('--publish must name an EdgeAnalysis directory');
}

function clearStageFiles(staging,part) {
  for(const extension of extensions){
    const file=stageBase(staging,part)+extension;
    if(fs.existsSync(file))fs.unlinkSync(file);
  }
}

export function prepareModelStage(staging,part) {
  clearStageFiles(staging,part);
  const reportFile=path.join(staging,'Reports',part.group,part.id+'.json');
  if(fs.existsSync(reportFile))fs.unlinkSync(reportFile);
  return reportFile;
}

// Only this invocation's cleared stage directory can supply current data.
// Failed native/export stages still expose current successful core analysis.
export function publishModelStage({publish,staging,part,groups,row}) {
  validatePublishDirectory(publish);
  const currentJson=stageBase(staging,part)+'.json';
  let currentData=null,currentError='';
  if(fs.existsSync(currentJson)){
    try{
      const data=read(currentJson);
      if(data.schema===1&&data.success===true&&Array.isArray(data.faces)&&Array.isArray(data.edges)&&data.counts)currentData=data;
      else currentError='Current edge-analysis JSON does not contain a successful core analysis';
    }catch(error){currentError=`Cannot read current edge-analysis JSON: ${error.message}`;}
  }else currentError='Native run produced no current edge-analysis JSON';
  if(row.sourceUnchanged===false){row.status='failed';row.error='Source neutral changed during this run';}
  if(!currentData){row.status='failed';row.error=row.error?`${row.error}; ${currentError}`:currentError;}
  if(row.status==='passed'){
    const missing=['.step','.brep'].filter(extension=>!fs.existsSync(stageBase(staging,part)+extension));
    if(missing.length){row.status='failed';row.error=`Native report passed but current stage artifacts are missing: ${missing.join(', ')}`;}
  }
  if(row.status!=='passed'){
    row.status='failed';row.error??=`Native run failed during ${row.stage??'edgeAnalysis'}`;
    // Archive every previous artifact, including old JSON, before publishing
    // a failed current record. The empty staging root never receives outputs.
    publishArtifacts(path.dirname(publish),path.join(staging,'empty-edge-analysis-publication'),part.group,part.id,groups,['EdgeAnalysis']);
  }
  const publicationStaging=currentData?staging:path.join(staging,'failed-edge-analysis-publication');
  if(!currentData)clearStageFiles(publicationStaging,part);
  const data=currentData??{
    schema:1,stage:'edgeAnalysis',name:part.group,success:false,complete:false,
    faces:[],edges:[],geometry:{faces:[],previewOnly:true},diagnostics:[],
    counts:{faces:0,edges:0,unresolvedSkin:0,unresolvedConvexity:0,skin:{},convexity:{}},
  };
  data.status=row.status;
  data.execution={status:row.status,stage:row.stage??'edgeAnalysis',error:row.error??null,
    sourceUnchanged:row.sourceUnchanged??null,code:row.code??null,timedOut:row.timedOut??false};
  if(row.status==='failed')data.diagnostics=[...(data.diagnostics??[]),row.error];
  write(stageBase(publicationStaging,part)+'.json',data);
  const artifacts=publishArtifacts(path.dirname(publish),publicationStaging,part.group,part.id,groups,['EdgeAnalysis']);
  row.artifacts=artifacts;row.counts=data.counts;row.complete=data.complete;
  return {id:row.id,name:part.group,status:row.status,error:row.error??null,
    dataUrl:'/data/'+encodeURIComponent(path.basename(artifacts.edgeAnalysisData)),
    faceCount:data.faces.length,edgeCount:data.edges.length,complete:data.complete,counts:data.counts};
}

async function main() {
const args=process.argv.slice(2);
const option=(key,fallback)=>args.includes(key)?args[args.indexOf(key)+1]:fallback;
const supported=new Set(['--native','--output','--publish','--workers','--timeout','--models','--checkpoints']);
for(const arg of args)if(arg.startsWith('--')&&!supported.has(arg))throw Error(`Unknown option ${arg}`);
const output=path.resolve(option('--output',path.join(repo,'output/tests/machining-edge-analysis-20261028')));
const native=path.resolve(option('--native',path.join(output,'native')));
const publish=path.resolve(option('--publish',path.join(repo,'TestData/Tube/EdgeAnalysis')));
validatePublishDirectory(publish);
const workers=Number(option('--workers','4'));
const timeout=Number(option('--timeout','600'))*1000;
if(!Number.isInteger(workers)||workers<1||workers>8||!Number.isFinite(timeout)||timeout<=0)throw Error('Invalid worker or timeout option');
const original=read(path.join(repo,'TestData/Tube/Reports/summary.json'));
const checkpointsFile=option('--checkpoints','');
const checkpoints=checkpointsFile?read(path.resolve(checkpointsFile)):{};
const oldCatalog=read(path.join(repo,'output/tests/cut-unit-stage1-20261025/manifest.json'));
const parts=original.parts;
const groups=parts.map(p=>p.group);
const baseFor=group=>{
  const file=path.basename(group),stem=path.parse(file).name;
  const duplicates=groups.filter(g=>path.parse(path.basename(g)).name.toLowerCase()===stem.toLowerCase());
  return duplicates.length>1?`${stem}__${path.extname(file).slice(1).toLowerCase()}`:stem;
};
const idFor=part=>oldCatalog.find(c=>c.file===baseFor(part.group)+'.step')?.id??parts.indexOf(part)+1;
const selection=option('--models','').split(',').filter(Boolean);
const chosen=parts.filter(p=>!selection.length||selection.includes(p.group)||selection.includes(String(idFor(p))));
if(!chosen.length)throw Error('No matching models');
const exe=path.join(native,'TubeStageProbe.exe');
const env={...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),path.join(repo,'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),process.env.PATH].join(path.delimiter)};
fs.mkdirSync(publish,{recursive:true});
const result={schema:1,stage:'edgeAnalysis',startedAt:new Date().toISOString(),status:'running',selected:chosen.length,
  source:'Latest imported neutral inputBRep from the current Classify report; prior stage artifacts remain untouched.',
  executable:{path:exe,sha256:hash(exe)},recognition:{path:path.join(native,'ExtrusionRecognition.dll'),sha256:hash(path.join(native,'ExtrusionRecognition.dll'))},
  rows:[]};
const catalogPath=path.join(publish,'manifest.json');
const catalog=fs.existsSync(catalogPath)?read(catalogPath):{schema:1,stage:'edgeAnalysis',models:[]};
const save=()=>{
  result.rows.sort((a,b)=>a.id-b.id);catalog.models.sort((a,b)=>Number(a.id)-Number(b.id));
  result.completed=result.rows.length;result.failed=result.rows.filter(r=>r.status!=='passed').length;
  write(path.join(output,'summary.json'),result);write(catalogPath,catalog);
};
async function run(part){
  const id=idFor(part),source=part.inputBRep,started=Date.now();
  const staging=path.join(output,'staging');
  const row={id,group:part.group,source,status:'failed',
    runtimeEvidence:{executable:result.executable,recognition:result.recognition}};
  row.reportFile=prepareModelStage(staging,part);
  try{
    if(!source||!fs.existsSync(source))throw Error(`Missing latest neutral input for ${part.group}`);
    row.sourceSha256=hash(source);
    let nativeArgs=['--part',source,staging,part.group,part.id];
    const checkpoint=checkpoints[part.group];
    if(checkpoint){
      if(checkpoint.sourceSha256!==row.sourceSha256)throw Error('Checkpoint source identity mismatch');
      for(const name of ['healed.icaxbrep','aligned.icaxbrep','split.icaxbrep','machining.icaxbrep','stock.icaxbrep','section-curves.icaxbrep','section-domains.json','performance.json'])
        if(typeof checkpoint.files?.[name]!=='string')throw Error(`Missing checkpoint digest: ${name}`);
      for(const [name,digest] of Object.entries(checkpoint.files??{}))
        if(hash(path.join(checkpoint.directory,name))!==digest)throw Error(`Checkpoint file changed: ${name}`);
      for(const proof of checkpoint.references??[])
        if(hash(proof.path)!==proof.sha256)throw Error('Checkpoint provenance reference changed');
      const performance=read(path.join(checkpoint.directory,'performance.json'));
      if(performance.status!=='passed'||performance.sourceNeutralUnchanged!==true||hash(performance.source)!==row.sourceSha256)
        throw Error('Checkpoint was not validated from the identical neutral source');
      if(hash(checkpoint.currentHealedPath)!==hash(path.join(checkpoint.directory,'healed.icaxbrep')))
        throw Error('Checkpoint healed geometry differs from the current source preparation');
      if(!Array.isArray(checkpoint.currentTransform4x4)||checkpoint.currentTransform4x4.length!==16
        ||JSON.stringify(checkpoint.currentTransform4x4)!==JSON.stringify(checkpoint.validatedTransform4x4))
        throw Error('Checkpoint coordinate-frame proof is missing or differs');
      row.upstreamCheckpoint=checkpoint;
      nativeArgs=['--edge-analysis-checkpoint',checkpoint.directory,staging,part.group,part.id];
    }
    const log=path.join(output,'logs',String(id).padStart(3,'0')+'.log');fs.mkdirSync(path.dirname(log),{recursive:true});
    const fd=fs.openSync(log,'w');
    try{
      Object.assign(row,await new Promise(resolve=>{
        let timedOut=false;
        const child=spawn(exe,nativeArgs,{cwd:path.join(repo,'src/x64/Debug'),env,windowsHide:true,stdio:['ignore',fd,fd]});
        const timer=setTimeout(()=>{timedOut=true;child.kill();},timeout);
        child.on('error',error=>{clearTimeout(timer);resolve({error:error.message,code:null,timedOut});});
        child.on('close',code=>{clearTimeout(timer);resolve({code,timedOut});});
      }));
    }finally{fs.closeSync(fd);}
    const report=fs.existsSync(row.reportFile)?read(row.reportFile):{};
    row.status=!row.timedOut&&row.code===0&&report.stages?.edgeAnalysis?.status==='passed'?'passed':'failed';
    row.stage=report.stage;row.analysis=report.stages?.edgeAnalysis;
    row.error=report.error??row.error;
    if(row.timedOut)row.error=`Process exceeded ${timeout/1000} seconds during ${row.stage??'import'}`;
  }catch(error){row.error=error.message;row.status='failed';}
  if(row.sourceSha256)row.sourceUnchanged=fs.existsSync(source)&&hash(source)===row.sourceSha256;
  row.seconds=(Date.now()-started)/1000;
  const model=publishModelStage({publish,staging,part,groups,row});
  catalog.models=catalog.models.filter(previous=>Number(previous.id)!==Number(id));catalog.models.push(model);
  result.rows.push(row);save();
  console.log(`${result.rows.length}/${chosen.length} case ${id} ${part.group}: ${row.status}${row.counts?' '+JSON.stringify(row.counts):' '+(row.error??row.stage)}`);
}
save();let cursor=0;
await Promise.all(Array.from({length:Math.min(workers,chosen.length)},async()=>{while(cursor<chosen.length)await run(chosen[cursor++]);}));
result.status=result.failed?'completed-with-failures':'passed';result.completedAt=new Date().toISOString();save();
process.exitCode=result.failed?1:0;
}

if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href)await main();
