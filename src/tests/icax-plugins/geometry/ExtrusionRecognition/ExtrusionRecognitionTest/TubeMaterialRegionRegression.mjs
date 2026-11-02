import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {publishArtifacts} from './tube_stage_outputs.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../../../..');
const root=path.join(repo,'TestData/Tube');
const output=path.join(root,'Reports/material-region-regression');
const native=path.join(repo,'output/tests/tube-stages/native-investigation');
const exe=path.join(native,'TubeStageProbe.exe');
// Fixture expectations are acceptance data, never production recognition rules.
const cases=[['样管-1.IGS',2],['1 cht (2)(1).stp',1],
  ['0.6）KD1551-4101 电子表扶手管简短5mm(1).IGS',1],
  ['修改2.STEP',0],['谢氏门栓22管.STEP',1],['左辊压梁.STEP',1]];
const groups=fs.readdirSync(path.join(root,'origin')).filter(f=>/\.(step|stp|igs|iges)$/i.test(f));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
fs.mkdirSync(output,{recursive:true});
const summary={status:'running',materialUnion:false,source:'fresh original imports',
  recognitionSha256:hash(path.join(native,'ExtrusionRecognition.dll')),results:[]};
const save=()=>fs.writeFileSync(path.join(output,'results.json'),JSON.stringify(summary,null,2));
save();
function run(args,log){
  return new Promise((resolve,reject)=>{
    const fd=fs.openSync(log,'w');
    const child=spawn(exe,args,{cwd:path.join(repo,'src/x64/Debug'),windowsHide:true,
      env:{...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),process.env.PATH].join(path.delimiter)},
      stdio:['ignore',fd,fd]});
    let timedOut=false;
    const timer=setTimeout(()=>{timedOut=true;child.kill();},120000);
    child.once('error',reject);
    child.once('close',code=>{clearTimeout(timer);fs.closeSync(fd);
      code===0&&!timedOut?resolve():reject(Error(`Native process ${timedOut?'timed out':`exit ${code}`}: ${log}`));});
  });
}
async function verify([group,expectedInnerCount]){
  const source=path.join(root,'origin',group),before=hash(source);
  const input=path.join(output,'inputs',group);fs.mkdirSync(input,{recursive:true});
  const staging=path.join(output,'staging');
  const record={source,sha256:before,expectedInnerCount,status:'running'};
  summary.results.push(record);save();
  try{
    await run(['--split',source,input],path.join(input,'import.log'));
    const manifest=JSON.parse(fs.readFileSync(path.join(input,'manifest.json')));
    assert.equal(manifest.parts.length,1,'Assembly passthrough must preserve the original model');
    for(const suffix of ['.json','.step','.svg','.raw.svg']){
      const p=path.join(staging,'Coutour',group,'part-1'+suffix);
      if(fs.existsSync(p))fs.unlinkSync(p);
    }
    await run(['--contour-only',path.join(input,'part-1.icaxbrep'),staging,group,'part-1'],path.join(input,'contour.log'));
    const contour=JSON.parse(fs.readFileSync(path.join(staging,'Coutour',group,'part-1.json')));
    const report=JSON.parse(fs.readFileSync(path.join(staging,'Reports',group,'part-1.json')));
    record.diagnostics=report.stages.contour.diagnostics;
    record.actualInnerCount=contour.wires.filter(w=>w.inner).length;
    record.wires=contour.wires.map(w=>({inner:w.inner,area:w.area,edges:w.edges.length,maxGapMm:w.maxGapMm}));
    assert.equal(report.stages.contour.status,'passed');
    assert.equal(contour.wires.filter(w=>!w.inner).length,1);
    assert.equal(record.actualInnerCount,expectedInnerCount,'Unexpected cavity count');
    const usedEdges=new Set();
    for(const wire of contour.wires){
      assert.ok(wire.closed&&wire.valid&&wire.maxGapMm<=0.0011&&wire.maxEndpointErrorMm<=0.0011);
      for(const edge of wire.edges){
        const endpoints=[edge.start,edge.end].map(p=>p.map(v=>v.toFixed(8)).join(',')).sort().join(';');
        const source=edge.sourceEdgeId==='0'?`face-${edge.sourceFaceId}:${endpoints}`:`edge-${edge.sourceEdgeId}`;
        const key=[source,Math.min(edge.first,edge.last).toPrecision(12),Math.max(edge.first,edge.last).toPrecision(12)].join(':');
        assert.ok(!usedEdges.has(key),'Undirected source edge interval reused between contours');usedEdges.add(key);
      }
    }
    const svg=fs.readFileSync(path.join(staging,'Coutour',group,'part-1.svg'),'utf8');
    const paths=[...svg.matchAll(/<path\b[^>]*\bd="([^"]+)"[^>]*\/>/g)];
    assert.equal(paths.length,expectedInnerCount+1);assert.ok(paths.every(p=>p[1].trim().endsWith('Z')));
    assert.equal((svg.match(/stroke="#00FF00"/g)??[]).length,1);
    assert.equal((svg.match(/stroke="#FF0000"/g)??[]).length,expectedInnerCount);
    assert.equal(hash(source),before,'Original source changed');
    record.artifacts=publishArtifacts(root,staging,group,'part-1',groups,['Align','Coutour']);
    record.status='passed';
  }catch(error){record.status='failed';record.error=error.message;}
  record.sourceUnchanged=hash(source)===before;
  save();console.log(`${group}: ${record.status}; expected ${expectedInnerCount}, actual ${record.actualInnerCount??'unavailable'}`);
}
let next=0;
await Promise.all(Array.from({length:2},async()=>{while(next<cases.length)await verify(cases[next++]);}));
summary.status='completed';summary.passed=summary.results.every(r=>r.status==='passed'&&r.sourceUnchanged);save();
process.exitCode=summary.passed?0:1;
