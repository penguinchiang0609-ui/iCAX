import test from 'node:test';
import assert from 'node:assert/strict';
import {encodeTaskProgress,decodeTaskProgress} from '../../apps/five-axis-cam/protocol/taskProgressPDOProtocol.mjs';
import {waitForTaskResult} from '../../apps/five-axis-cam/webpage/taskProgressStream.mjs';
import {planMachineMotion} from '../../apps/five-axis-cam/webpage/machineMotionPlan.mjs';
import {checkCollision,repairCollision} from '../../apps/five-axis-cam/webpage/simulationCollision.mjs';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machineResourceTransport} from './fixtures/machineJSONResource.mjs';

const initial={epoch:'epoch',taskId:'task',status:'Running',progressPDO:{id:'12',version:1,payloadSize:65536,arenaName:'arena'}};
const telemetry=(sequence,status='Running',extra={})=>encodeTaskProgress({taskId:'task',epoch:'epoch',sequence,status,kind:'MachinePlan',progress:0,...extra});
function endpoint(frames,{status='Completed',result={accepted:true}}={}){
 const calls=[],seen=[];let reads=0;
 const sceneProxy={pdo:{enabled:true,async withReadDescriptor(descriptor,reader){calls.push(['pdo',descriptor.id]);const bytes=frames[Math.min(reads++,frames.length-1)];return reader(bytes.buffer,{dataVersion:String(reads)});}},async invoke(method,request){calls.push([method,request]);return {...initial,status,result};}};
 return {context:{sceneProxy},calls,seen,readCount:()=>reads};
}
const fast={schedule:fn=>setImmediate(fn),unschedule:id=>clearImmediate(id)};

test('official TaskProgress binary preserves uint64 and exact native local indices without results',()=>{
 const bytes=telemetry('18446744073709551615','Running',{collisionProgress:{stage:'Intervals',completedSegmentCount:'44',totalSegmentCount:'44',fullSegmentCount:'3607',currentSegmentIndex:'2661',lastCompletedSegmentIndex:'2660'}});
 const packed=new Uint8Array(bytes.length+8);packed.set(bytes,4);
 const decoded=decodeTaskProgress(packed.subarray(4,-4));assert.equal(decoded.sequence,'18446744073709551615');
 assert.equal(decoded.collisionProgress.currentSegmentIndex,'2661');assert.equal(decoded.collisionProgress.completedSegmentCount,'44');assert.equal(decoded.result,undefined);assert.equal(decoded.frames,undefined);
 assert.throws(()=>telemetry(1,'Running',{result:{frames:[1]}}),/进度内容/);
});
test('TaskProgress rejects wrong schema envelope, truncated vectors and malformed details',()=>{
 const bytes=telemetry(1);const wrong=bytes.slice();wrong[4]=0;assert.throws(()=>decodeTaskProgress(wrong));
 assert.throws(()=>decodeTaskProgress(bytes.subarray(0,Math.floor(bytes.length/2))));
 assert.throws(()=>telemetry(0));assert.throws(()=>telemetry(1,'invented'));
 assert.throws(()=>telemetry(Number.MAX_SAFE_INTEGER+1));assert.throws(()=>telemetry(1,'Running',{machineProgress:[]}));
});
test('intermediate native progress stays in shared memory and exactly one terminal SDO returns complete result',async()=>{
 const f=endpoint([telemetry(1,'Running',{machineProgress:{stage:'Solving',currentStation:'7'}}),telemetry(2,'Running',{machineProgress:{stage:'CheckingMotion',currentStation:'8'}}),telemetry(3,'Completed')]);
 const result=await waitForTaskResult(f.context,initial,{...fast,onProgress:p=>f.seen.push(p)});
 assert.deepEqual(f.seen.map(p=>p.machineProgress?.stage),['Solving','CheckingMotion',undefined]);
 assert.equal(f.readCount(),3);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.CancelTask').length,0);
 assert.deepEqual(result.result,{accepted:true});assert.ok(f.seen.every(p=>!('result'in p)));
});
test('task identity, epoch and output sequence fences ignore stale snapshots',async()=>{
 const wrongTask=encodeTaskProgress({taskId:'old',epoch:'epoch',sequence:99,status:'Completed'}),wrongEpoch=encodeTaskProgress({taskId:'task',epoch:'old',sequence:100,status:'Completed'});
 const f=endpoint([wrongTask,wrongEpoch,telemetry(2),telemetry(1),telemetry(2),telemetry(3,'Completed')]);
 await waitForTaskResult(f.context,initial,{...fast,onProgress:p=>f.seen.push(p)});
 assert.deepEqual(f.seen.map(p=>p.sequence),['2','3']);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);
});
test('a missing PDO cancels reliably and never starts QueryTask polling fallback',async()=>{
 const calls=[],context={sceneProxy:{async invoke(method){calls.push(method);return {...initial,status:'Cancelled'};}}};
 await assert.rejects(waitForTaskResult(context,initial),/实时进度通道/);
 assert.deepEqual(calls,['CamSimulation.CancelTask']);
});
test('project disposal during a mapped read silently drains its terminal and retires via one reliable query',async()=>{
 const f=endpoint([telemetry(1)],{status:'Cancelled'});let release,active=true;f.context.sceneProxy.pdo.withReadDescriptor=async(_,reader)=>{await new Promise(r=>release=r);return reader(telemetry(9,'Cancelled').buffer,{dataVersion:'9'});};
 const waiting=waitForTaskResult(f.context,initial,{...fast,active:()=>active,onProgress:p=>f.seen.push(p),cancel:async()=>{f.calls.push(['cancel']);return initial;}});
 active=false;release();assert.equal(await waiting,null);assert.deepEqual(f.seen,[]);assert.deepEqual(f.calls.map(c=>c[0]),['cancel','CamSimulation.QueryTask']);
});
test('a stalled PDO producer cancels once with a finite watchdog and never polls the mailbox',async()=>{
 const f=endpoint([telemetry(1)]);await assert.rejects(waitForTaskResult(f.context,initial,{...fast,timeoutMs:15}),/长时间未更新/);
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.CancelTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,0);
 assert.equal(f.calls.find(c=>c[0]==='CamSimulation.CancelTask')[1].releaseProgress,true);
});
test('live native sequence heartbeats keep a long unchanged task alive without inventing progress',async()=>{
 const frames=Array.from({length:10},(_,i)=>telemetry(i+1));frames.push(telemetry(11,'Completed'));const f=endpoint(frames);
 const started=Date.now();await waitForTaskResult(f.context,initial,{schedule:fn=>setTimeout(fn,8),unschedule:clearTimeout,timeoutMs:40,onProgress:p=>f.seen.push(p)});
 assert.ok(Date.now()-started>40);assert.equal(f.readCount(),11);assert.ok(f.seen.every(p=>p.progress==='0'));
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.CancelTask').length,0);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);
});
test('collision check consumes actual segment telemetry and one final report query before autosave',async()=>{
 const progress={stage:'Intervals',completedSegmentCount:'0',totalSegmentCount:'2',fullSegmentCount:'2',currentSegmentIndex:'1'};
 const f=endpoint([telemetry(1,'Running',{collisionProgress:progress}),telemetry(2,'Completed',{collisionProgress:{...progress,completedSegmentCount:'2',lastCompletedSegmentIndex:'1'}})]);
 const invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>method==='CamSimulation.CheckCollision'?(f.calls.push([method]),initial):invoke(method,request);
 Object.assign(f.context,{mount:{querySelector:()=>null},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/check.i5cam'},async autosave(){f.calls.push(['save']);}}});
 const view={scene:{domain:{expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'s'}}},s={cursor:0,frames:[0,1,2].map(x=>({position:new THREE.Vector3(x,0,0),rotation:new THREE.Quaternion()}))},poses=[];
 assert.equal(await checkCollision(f.context,view,s,{renderProject(){},async refreshSceneState(){f.calls.push(['refresh']);}},()=>true,()=>poses.push(s.cursor)),true);
 assert.ok(poses.includes(1));assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='save').length,1);assert.equal(view.pending,false);
});
test('local repair progress remains local and its one final committed result owns one autosave',async()=>{
 const f=endpoint([telemetry(1,'Running',{repairProgress:{stage:'Verifying',scope:'Local',candidateCount:'7',completedSegmentCount:'2',totalSegmentCount:'4'}}),telemetry(2,'Completed')],{result:{status:'Repaired',changed:true,checkedSegmentCount:'4',reusedSegmentCount:'3603'}});
 const invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>method==='CamSimulation.AutoRepairCollision'?(f.calls.push([method,request]),initial):invoke(method,request);
 Object.assign(f.context,{mount:{querySelector:()=>null},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/repair.i5cam'},async autosave(){f.calls.push(['save']);}}});
 const view={scene:{domain:{expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'s'}}},s={cursor:0,collisionReport:{validity:'Current',segments:[{index:'0',status:'Collision'}]},frames:[{role:'Cut',intentId:'cut',target:new THREE.Vector3()}]};
 assert.equal(await repairCollision(f.context,view,s,{renderProject(){},async refreshSceneState(){f.calls.push(['refresh']);}},()=>true),true);
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='save').length,1);assert.equal(s.repairResult.checkedSegmentCount,'4');assert.equal(view.pending,false);
});
test('unchanged and foreign frames cannot renew the producer watchdog',async()=>{
 const f=endpoint([encodeTaskProgress({taskId:'other',epoch:'epoch',sequence:100,status:'Completed'})]);
 await assert.rejects(waitForTaskResult(f.context,initial,{...fast,timeoutMs:15}),/长时间未更新/);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.CancelTask').length,1);
});
test('terminal failures are queried once and preserve authoritative error detail',async()=>{
 const f=endpoint([telemetry(1,'Failed',{error:'short telemetry'})],{status:'Failed',result:{detail:'full result'}});
 const result=await waitForTaskResult(f.context,initial,fast);assert.equal(result.status,'Failed');assert.deepEqual(result.result,{detail:'full result'});
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);
});
test('real plan UI flow adopts PDO progress then saves and refreshes only the terminal certified plan',async()=>{
 const origin=new THREE.Matrix4().toArray(),report={schema:'cam.machine-motion-plan',schemaVersion:2,interpolation:'QuinticNormalized',status:'Ready',validity:'Current',playbackAllowed:true,modelKey:'m',revision:{revision:'native-plan-1'},tool:1,support:0,
  nodes:[{id:'table',parent:-1,type:'fixed',axis:[0,0,1],origin},{id:'head',parent:0,type:'prismatic',axis:[0,0,1],origin}],sampleCount:'1',durationSeconds:0,frames:[{joints:[0,0],durationSeconds:0,laser:false}]};
 const transport=machineResourceTransport(),metadata=transport.wire(report,'planResourceRef');
 const f=endpoint([telemetry(1,'Running',{machineProgress:{stage:'Solving'}}),telemetry(2,'Completed')],{result:{accepted:true}});f.context.sceneProxy.resources=transport.resources;
 const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'s',workpieces:[],placements:[],intentToolpaths:[],setups:[{id:'s',generatedGeometry:{status:'Current',operations:[{intentId:'cut'}],transitions:[]}}]};
 const invoke=f.context.sceneProxy.invoke;f.context.sceneProxy.invoke=async(method,request)=>{
  if(method==='CamSimulation.PlanMachine'){f.calls.push([method,request]);return initial;}
  if(['CamSimulation.ReadMachineAttempt','CamSimulation.ReadMachinePlan'].includes(method)){f.calls.push([method,request]);return metadata;}
  if(method==='CamScene.ReadState'){f.calls.push([method,request]);return structuredClone(domain);}
  return invoke(method,request);
 };
 Object.assign(f.context,{mount:{querySelector:()=>null},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/plan.i5cam'},async autosave(){f.calls.push(['save']);}}});
 const view={scene:{domain:structuredClone(domain)}},s={};
 await planMachineMotion(f.context,view,s,{renderProject(){},async refreshSceneState(){assert.fail('Planning must refresh its own state without replacing the scene');}});
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.QueryTask').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.PlanMachine').length,1);
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.ReadMachinePlan').length,1);
 assert.equal(f.calls.filter(c=>c[0]==='CamSimulation.ReadMachineAttempt').length,1);assert.equal(f.calls.filter(c=>c[0]==='CamScene.ReadState').length,2);
 const save=f.calls.findIndex(c=>c[0]==='save'),refresh=f.calls.findLastIndex(c=>c[0]==='CamScene.ReadState');
 assert.ok(save>=0&&save<refresh);assert.ok(refresh<f.calls.findIndex(c=>c[0]==='CamSimulation.ReadMachinePlan'));assert.deepEqual(s.report,report);assert.equal(s.planning,false);
});
