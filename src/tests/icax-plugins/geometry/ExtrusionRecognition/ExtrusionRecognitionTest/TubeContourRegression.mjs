import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {publishArtifacts} from './tube_stage_outputs.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../../../..');
const root=path.join(repo,'TestData/Tube');
const output=path.join(root,'Reports/contour-regression');
const native=path.join(repo,'output/tests/tube-stages/native-overlay');
const exe=path.join(native,'TubeStageProbe.exe');
// Acceptance counts come from the user's original-part inspection, not the
// current algorithm's output. Material partition loops are not cavities.
const cases=[['左辊压梁.STEP',1],['样管-1.IGS',2],['修改2.STEP',0],['谢氏门栓22管.STEP',1]];
const groups=fs.readdirSync(path.join(root,'origin')).filter(f=>/\.(step|stp|igs|iges)$/i.test(f));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const results=[];
fs.mkdirSync(output,{recursive:true});
function run(args,log){
  const fd=fs.openSync(log,'w');
  const result=spawnSync(exe,args,{cwd:path.join(repo,'src/x64/Debug'),windowsHide:true,
    env:{...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),process.env.PATH].join(path.delimiter)},
    timeout:120000,stdio:['ignore',fd,fd]});
  fs.closeSync(fd);assert.equal(result.status,0,`Native regression failed: ${log}`);
}
for(const [group,innerCount] of cases){
  const source=path.join(root,'origin',group),before=hash(source);
  const input=path.join(output,'inputs',group);fs.mkdirSync(input,{recursive:true});
  run(['--split',source,input],path.join(input,'import.log'));
  const staging=path.join(output,'staging');
  for(const suffix of ['.json','.step','.svg','.raw.svg']){
    const file=path.join(staging,'Coutour',group,'part-1'+suffix);
    if(fs.existsSync(file))fs.unlinkSync(file);
  }
  run(['--contour-only',path.join(input,'part-1.icaxbrep'),staging,group,'part-1'],path.join(input,'contour.log'));
  const contour=JSON.parse(fs.readFileSync(path.join(staging,'Coutour',group,'part-1.json')));
  const report=JSON.parse(fs.readFileSync(path.join(staging,'Reports',group,'part-1.json')));
  assert.equal(report.stages.contour.status,'passed');
  assert.equal(contour.wires.filter(w=>!w.inner).length,1);
  assert.equal(contour.wires.filter(w=>w.inner).length,innerCount);
  for(const wire of contour.wires){
    assert.ok(wire.closed&&wire.valid);assert.ok(wire.maxGapMm<=0.0011);
    assert.ok(wire.maxEndpointErrorMm<=0.0011);
    const seen=new Set();
    for(const edge of wire.edges){
      const key=[edge.start,edge.end].map(p=>p.map(v=>v.toFixed(5)).join(',')).sort().join('|');
      assert.ok(!seen.has(key),'Repeated undirected edge');seen.add(key);
    }
  }
  const svg=fs.readFileSync(path.join(staging,'Coutour',group,'part-1.svg'),'utf8');
  const paths=[...svg.matchAll(/<path\b[^>]*\bd="([^"]+)"[^>]*\/>/g)];
  assert.equal(paths.length,innerCount+1);assert.ok(paths.every(p=>p[1].trim().endsWith('Z')));
  assert.equal((svg.match(/stroke="#00FF00"/g)??[]).length,1);
  assert.equal((svg.match(/stroke="#FF0000"/g)??[]).length,innerCount);
  assert.equal(hash(source),before,'Original changed');
  const artifacts=publishArtifacts(root,staging,group,'part-1',groups,['Align','Coutour']);
  results.push({source,sha256:before,status:'passed',outerCount:1,innerCount,
    wires:contour.wires.map(w=>({inner:w.inner,edges:w.edges.length,area:w.area,maxGapMm:w.maxGapMm,maxEndpointErrorMm:w.maxEndpointErrorMm})),artifacts});
  console.log(`${group}: 1 outer, ${innerCount} minimal inner loops; STEP and SVG verified`);
}
fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({passed:true,executableSha256:hash(exe),
  recognitionSha256:hash(path.join(native,'ExtrusionRecognition.dll')),results},null,2));
