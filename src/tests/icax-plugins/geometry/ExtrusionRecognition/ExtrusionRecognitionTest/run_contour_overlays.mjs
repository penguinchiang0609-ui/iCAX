import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {publishArtifacts} from './tube_stage_outputs.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../../../..');
const args=process.argv.slice(2),option=(name,value)=>args.includes(name)?args[args.indexOf(name)+1]:value;
const root=path.resolve(option('--root',path.join(repo,'TestData/Tube')));
const native=path.resolve(option('--native',path.join(repo,'output/tests/tube-stages/native-overlay')));
const workers=Number(option('--workers','4')),timeoutMs=Number(option('--timeout','120'))*1000;
const exe=path.join(native,'TubeStageProbe.exe'),runtime=path.join(repo,'src/x64/Debug');
const read=file=>{try{return JSON.parse(fs.readFileSync(file,'utf8'));}catch{return null;}};
const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2));};
const summaryFile=path.join(root,'Reports/summary.json');
let baseline;
// Let the baseline and its one-time flat-file publisher finish before replacing outputs.
while(!(baseline=read(summaryFile))||baseline.status!=='completed'||baseline.parts.some(p=>!p.artifacts))
  await new Promise(r=>setTimeout(r,2000));
const parts=baseline.parts,groups=baseline.sources.map(s=>s.group),staging=path.join(root,'Reports/overlay-staging');
const summary={startedAt:new Date().toISOString(),status:'running',sourceCount:parts.length,
  executable:exe,sha256:crypto.createHash('sha256').update(fs.readFileSync(exe)).digest('hex'),
  meaning:'Export checks only; recognition invariant results remain in summary.json.',parts:[]};
const output=path.join(root,'Reports/contour-overlays.json');let cursor=0;
await Promise.all(Array.from({length:workers},async()=>{
  while(cursor<parts.length){
    const p=parts[cursor++],reportFile=path.join(staging,'Reports',p.group,p.id+'.json');
    fs.mkdirSync(path.dirname(reportFile),{recursive:true});
    if(fs.existsSync(reportFile))fs.unlinkSync(reportFile);
    const log=fs.openSync(path.join(path.dirname(reportFile),p.id+'.log'),'w');
    const processResult=await new Promise(resolve=>{
      let timedOut=false,settled=false;
      const child=spawn(exe,['--contour-only',p.brep,staging,p.group,p.id],{cwd:runtime,
        env:{...process.env,PATH:[native,runtime,process.env.PATH].join(path.delimiter)},windowsHide:true,stdio:['ignore',log,log]});
      const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutMs);
      const finish=(code,error)=>{if(settled)return;settled=true;clearTimeout(timer);fs.closeSync(log);resolve({code,error,timedOut});};
      child.on('error',e=>finish(null,e.message));child.on('close',code=>finish(code));
    });
    const report=read(reportFile),exported=!!report?.stages?.contour?.overlayRoundTrip,verified=report?.stages?.contour?.overlayRoundTrip?.passed===true;
    const result={group:p.group,exported,verified,process:processResult,report:reportFile,error:report?.error??processResult.error,
      roundTrip:report?.stages?.contour?.overlayRoundTrip};
    if(exported){
      result.artifacts=publishArtifacts(root,staging,p.group,p.id,groups,['Align','Coutour']);
      p.artifacts={...p.artifacts,...result.artifacts};
    }else{
      // Keep earlier contour-only files as diagnostics, outside the review directory.
      for(const key of ['contour','contourData'])if(p.artifacts?.[key]&&fs.existsSync(p.artifacts[key])){
        const old=p.artifacts[key],archive=path.join(root,'Reports/uncolored-contours',path.basename(old));
        fs.mkdirSync(path.dirname(archive),{recursive:true});fs.renameSync(old,archive);delete p.artifacts[key];
      }
    }
    p.contourOverlay={exported,verified,report:reportFile,error:result.error};
    write(p.reportPath,p);summary.parts.push(result);write(output,summary);
    console.log(`Contour overlay ${summary.parts.length}/${parts.length}: ${p.group} -> ${verified?'verified':exported?'exported; validation failed':processResult.timedOut?'timeout':'unavailable'}`);
  }
}));
summary.status='completed';summary.completedAt=new Date().toISOString();
summary.counts={exported:summary.parts.filter(p=>p.exported).length,verified:summary.parts.filter(p=>p.verified).length,
  validationFailed:summary.parts.filter(p=>p.exported&&!p.verified).length,unavailable:summary.parts.filter(p=>!p.exported).length};
summary.parts.sort((a,b)=>a.group.localeCompare(b.group));write(output,summary);
baseline.outputCoordinates='Aligned +X body; exact green outer/red inner contours at minimum-X end. STEP retains BRep body and colors.';
baseline.contourOverlays={...summary.counts,report:output};write(summaryFile,baseline);
console.log(JSON.stringify(summary.counts));
