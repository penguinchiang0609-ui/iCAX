import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serializeVariantText } from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

const root=fileURLToPath(new URL('../../../',import.meta.url));
const output=resolve(root,'output/tests/order-self-acceptance-fixture');mkdirSync(output,{recursive:true});
const directory=resolve(root,'src/x64/Debug');
const child=spawn(resolve(directory,'FiveAxisOrderTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:directory,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0;const pending=new Map(),log=[];
const fail=error=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(error);}pending.clear();};
child.on('error',fail);child.on('exit',code=>{if(pending.size)fail(Error(`Native process exited ${code}`));});
child.stderr.on('data',data=>log.push(String(data)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}
  const value=JSON.parse(line.slice(12)),p=pending.get(value.id);if(!p)return;clearTimeout(p.timer);pending.delete(value.id);value.ok?p.resolve(value.result):p.reject(Error(value.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++sequence;pending.set(id,{resolve,reject,timer:setTimeout(()=>{pending.delete(id);reject(Error(`Native timeout: ${message.method||message.action}`));},180000)});
  child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload??{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const read=()=>invoke('CamScene.ReadState'),setup=s=>s.setups.find(x=>x.id===s.activeSetupId);
const request=s=>({commandId:crypto.randomUUID(),expected:s.expected,setupId:s.activeSetupId});
const wait=async task=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
const travel=async()=>{let s=await read();await wait(await invoke('CamRapid.Generate',{...request(s),geometryRevision:setup(s).generatedGeometry.revision}));
  const transitions=setup(await read()).generatedGeometry.transitions;return {clear:transitions.filter(t=>t.validation==='Clear').length,blocked:transitions.filter(t=>t.validation!=='Clear').map(t=>({from:t.from,to:t.to,reason:t.reason})),seconds:transitions.filter(t=>t.validation==='Clear').reduce((sum,t)=>sum+t.estimatedSeconds,0)};};
try {
  const info=await rpc({action:'info'});await rpc({action:'open-project',path:resolve(root,'output/tests/order-optimization-disk-final/comparison-model.i5cam')});
  let state=await read();const baseline=JSON.parse(readFileSync(resolve(root,'output/tests/order-optimization-disk-final/comparison.json'),'utf8')).baseline.ids;
  assert.deepEqual([...baseline].sort(),[...setup(state).definitionOrder].sort());
  const actualRapids=setup(state).generatedGeometry.transitions;
  const imported=await wait(await invoke('CamFixture.Import',{...request(state),sourcePath:info.fixturePath}));
  const fixtureId=imported.result.fixtureId;state=await read();
  // Place a real solid obstacle in the middle of a long proven travel segment,
  // rather than inventing points or mocking a collision result.
  let selected;for(const connection of actualRapids){assert.equal(connection.validation,'Clear');for(let i=1;i<connection.points.length;++i){const a=connection.points[i-1],b=connection.points[i];
    const length=Math.hypot(...a.map((v,n)=>v-b[n]));if(!selected||length>selected.length)selected={a,b,length};}}
  const center=selected.a.map((v,n)=>(v+selected.b[n])/2);
  const scenarios=[];
  for(const offset of [0,40]){
    state=await read();const transform=[1,0,0,center[0]-15,0,1,0,center[1]-15+offset,0,0,1,center[2]-30,0,0,0,1];
    await invoke('CamFixture.Update',{...request(state),fixtureId,transform});
    state=await read();await wait(await invoke('CamPlan.Generate',request(state)));
    await invoke('CamPlan.SetOrder',{...request(await read()),mode:'Manual',intentIds:baseline});const before=await travel();
    const sorted=await invoke('CamPlan.SetOrder',{...request(await read()),mode:'Automatic'});const after=await travel();
    scenarios.push({offsetMm:offset,fixtureTransform:transform,before,after,optimization:sorted.optimization,
      worsenedBlockedCount:after.blocked.length>before.blocked.length,
      worsenedClearTravelTime:before.blocked.length===0&&after.blocked.length===0&&after.seconds>before.seconds+1e-6});
    console.log('Fixture scenario',JSON.stringify(scenarios.at(-1)));
  }
  // Required safety failure: increase clearance beyond the available endpoint
  // space and verify no rejected connector is qualified as executable.
  state=await read();await invoke('CamRapid.Configure',{...request(state),settings:{...setup(state).rapidSettings,collisionThresholdMm:1000}});
  state=await read();await wait(await invoke('CamPlan.Generate',request(state)));const rejected=await travel();assert.equal(rejected.clear,0);
  const orderingWithObstaclesPassed=scenarios.every(s=>!s.worsenedBlockedCount&&!s.worsenedClearTravelTime);
  const result={passed:orderingWithObstaclesPassed,scenarios,obstructedOriginalSegment:selected,forcedRejection:rejected,fixtureId,
    scope:'Native production SDO, real disk STEP and solid fixture; no desktop automation or real machine execution',orderingWithObstaclesPassed};
  writeFileSync(resolve(output,'report.json'),JSON.stringify(result,null,2));
} finally {writeFileSync(resolve(output,'native.log'),log.join('\n'));try{await rpc({action:'exit'});}catch{}child.stdin.end();}
