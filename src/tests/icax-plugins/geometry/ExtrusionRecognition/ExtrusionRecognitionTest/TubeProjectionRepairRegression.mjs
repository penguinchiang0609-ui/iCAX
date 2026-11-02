import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../../../..');
const args=process.argv.slice(2);
const option=(name,fallback)=>{const i=args.indexOf(name);return i<0?fallback:args[i+1];};
const root=path.resolve(option('--root',path.join(repo,'TestData/Tube/Reports/projection-repair-20261019')));
const native=path.resolve(option('--native',path.join(repo,'output/tests/tube-stages/native-investigation')));
const workers=Number(option('--workers','4')),timeout=Number(option('--timeout','240'))*1000;
const contourOnly=args.includes('--contour-only');
const baselinePath=path.resolve(option('--baseline',path.join(repo,'TestData/Tube/Reports/material-region-batch-20261019/summary.json')));
const baseline=JSON.parse(fs.readFileSync(baselinePath));
const previous=JSON.parse(fs.readFileSync(path.join(repo,'output/tests/tube-stages/end-boundary-regression/results.json')));
const previousByName=new Map(previous.results.map(row=>[row.group,row]));
const only=option('--only','');
const parts=baseline.parts.filter(row=>!only||only.split('|').includes(row.group));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const nativeHash=hash(path.join(native,'ExtrusionRecognition.dll'));
const report={completed:false,baseline:baselinePath,mode:contourOnly?'align-and-contour':'align-contour-classify',
  importedModels:parts.length,importPolicy:'Reuse imported BRep only when original file SHA256 matches baseline; import not rerun',
  recognitionSha256:nativeHash,results:[]};
fs.mkdirSync(root,{recursive:true});
const save=()=>fs.writeFileSync(path.join(root,'results.json'),JSON.stringify(report,null,2));
save();let cursor=0;
await Promise.all(Array.from({length:workers},async()=>{
  while(cursor<parts.length){
    const part=parts[cursor++],expected=baseline.sources.find(row=>row.group===part.group).sha256;
    if(hash(part.source)!==expected)throw Error(`Source changed: ${part.group}`);
    if(hash(path.join(native,'ExtrusionRecognition.dll'))!==nativeHash)throw Error('Recognition DLL changed during regression');
    const log=path.join(root,part.group+'.log'),fd=fs.openSync(log,'w');const start=Date.now();
    const processResult=await new Promise(resolve=>{
      let timedOut=false,settled=false;
      const child=spawn(path.join(native,'TubeStageProbe.exe'),[contourOnly?'--contour-only':'--part',part.brep,root,part.group,'part-1'],
        {cwd:path.join(repo,'src/x64/Debug'),windowsHide:true,env:{...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),process.env.PATH].join(';')},stdio:['ignore',fd,fd]});
      const timer=setTimeout(()=>{timedOut=true;child.kill();},timeout);
      const finish=(code,error)=>{if(settled)return;settled=true;clearTimeout(timer);fs.closeSync(fd);resolve({code,error,timedOut,seconds:(Date.now()-start)/1000});};
      child.on('error',e=>finish(null,e.message));child.on('close',code=>finish(code));
    });
    const file=path.join(root,'Reports',part.group,'part-1.json');
    const result=fs.existsSync(file)?JSON.parse(fs.readFileSync(file)):{};
    if(result.status==='running'){
      result.status=processResult.timedOut?'timeout':'crashed';
      result.stages??={};result.stages[result.stage]={status:result.status,
        error:processResult.timedOut?'Native stage exceeded timeout':`Native process exited with code ${processResult.code}`};
    }
    const contour=result.stages?.contour;
    const before=previousByName.get(part.group);
    const row={group:part.group,source:part.source,sourceUnchanged:hash(part.source)===expected,
      status:result.status,stage:result.stage,process:processResult,stages:result.stages,
      beforeStages:part.stages,
      beforeContour:before?.after??part.stages?.contour?.status,
      afterContour:contour?.status??'missing',
      beforeInner:before?.afterInner??((part.stages?.contour?.wireCount??1)-1),
      afterInner:contour?.wireCount===undefined?null:contour.wireCount-1,
      beforeDirection:part.stages?.align?.direction,afterDirection:result.stages?.align?.direction,
      report:file};
    row.contourChangedToFailure=row.beforeContour==='passed'&&row.afterContour!=='passed';
    report.results.push(row);save();
    console.log(`${report.results.length}/${parts.length}: ${part.group}: ${result.status} (${result.stage})`);
  }
}));
report.completed=true;
report.sourcesUnchanged=report.results.every(row=>row.sourceUnchanged);
report.nativeUnchanged=hash(path.join(native,'ExtrusionRecognition.dll'))===nativeHash;
report.counts={};
for(const stage of ['align','contour','classify']){
  report.counts[stage]={};
  for(const row of report.results){const status=row.stages?.[stage]?.status??(row.stage===stage&&row.process.timedOut?'timeout':'blocked');
    report.counts[stage][status]=(report.counts[stage][status]??0)+1;}
}
report.changedInnerCounts=report.results.filter(row=>row.beforeContour==='passed'&&row.afterContour==='passed'&&row.beforeInner!==row.afterInner)
  .map(({group,beforeInner,afterInner})=>({group,beforeInner,afterInner}));
report.changedToFailure=report.results.filter(row=>row.contourChangedToFailure).map(({group,stages})=>({group,diagnostics:stages?.contour?.diagnostics}));
save();console.log(JSON.stringify({counts:report.counts,changedInnerCounts:report.changedInnerCounts,
  changedToFailure:report.changedToFailure,sourcesUnchanged:report.sourcesUnchanged,nativeUnchanged:report.nativeUnchanged},null,2));
