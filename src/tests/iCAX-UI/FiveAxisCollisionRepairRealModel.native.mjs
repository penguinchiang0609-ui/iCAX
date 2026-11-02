import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,copyFileSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {hydratePathSamples} from '../../apps/five-axis-cam/webpage/pathSampleResource.mjs';

const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY),output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),source=resolve(process.env.ICAX_REPAIR_PROJECT);
mkdirSync(output,{recursive:true});const project=resolve(output,'input-copy.i5cam');assert.notEqual(project,source);copyFileSync(source,project);
const native=spawn(resolve(runtime,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0;const pending=new Map(),logs=[];
createInterface({input:native.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){logs.push(line);return;}const reply=JSON.parse(line.slice(12)),wait=pending.get(reply.id);if(!wait)return;pending.delete(reply.id);clearTimeout(wait.timer);reply.ok?wait.resolve(reply.result):wait.reject(Error(reply.error));});
native.stderr.on('data',data=>logs.push(String(data)));native.on('exit',code=>{for(const wait of pending.values()){clearTimeout(wait.timer);wait.reject(Error('Native exited '+code));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(Error('Native RPC timeout '+message.action));},180000);pending.set(id,{resolve,reject,timer});native.stdin.write(JSON.stringify({id,...message})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)});
const resources={get:async(url,{headers})=>{const reply=await rpc({action:'resource',url,version:headers['ICAX-Resource-Version'],encoding:'base64',head:false});const bytes=Buffer.from(reply.base64,'base64');return {ok:true,headers:{get:()=>reply.version},arrayBuffer:async()=>bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength)};}};
const read=async()=>hydratePathSamples(await invoke('CamScene.ReadState'),{resources});
const setup=state=>state.setups.find(s=>s.id===state.activeSetupId);
const actualGeometry=state=>{const {sampleGeometryRef,...geometry}=setup(state).generatedGeometry;return geometry;};
const command=async(method,extra={})=>{const state=await read();return invoke(method,{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,...extra});};
const write=(name,data)=>writeFileSync(resolve(output,name),JSON.stringify(data,null,2));
async function wait(task,label,maximum){const start=Date.now();let last=0;while(task.status==='Running'){
  await new Promise(resolve=>setTimeout(resolve,250));task=await invoke('CamSimulation.QueryTask',{epoch:task.epoch,taskId:task.taskId});
  if(Date.now()-last>10000){last=Date.now();console.log(JSON.stringify({label,elapsedMs:Date.now()-start,status:task.status,collisionProgress:task.collisionProgress,repairProgress:task.repairProgress}));}
  if(Date.now()-start>maximum)throw Error(label+' timed out');
}return task;}
const immutableCuts=state=>setup(state).generatedGeometry.operations.map(op=>({id:op.intentId,curveRef:op.curveRef,parts:op.parts.map(p=>{assert.ok(Array.isArray(p.points)&&p.points.length>0,'Actual native path resource must be decoded');return {role:p.role,points:p.points,sourceSamples:p.sourceSamples};})}));
try{
  await rpc({action:'open-project',path:project});let before=await read();assert.equal(setup(before).generatedGeometry.status,'Current');
  const outer=setup(before).generatedGeometry.operations.findLast(op=>op.boundaryRole==='OuterTrim')??setup(before).generatedGeometry.operations.at(-1);
  const policies=await invoke('CamProcess.Read',{expected:before.expected,intentIds:[outer.intentId]});write('selected-policy.json',policies);console.log(JSON.stringify({intentId:outer.intentId,posePolicy:policies.items[0].posePolicy}));
  if(!process.env.ICAX_REPAIR_POLICY_ONLY){
  const checkStart=Date.now();if(!process.env.ICAX_REPAIR_REUSE_CURRENT){const check=await wait(await command('CamSimulation.CheckCollision'),'fresh-full-witness',900000);assert.equal(check.status,'Completed',check.error);write('fresh-task.json',check);}before=await read();write('before-report.json',setup(before).simulationCheck);await rpc({action:'export-project',path:resolve(output,'freshly-checked.i5cam')});
  const report=setup(before).simulationCheck;assert.equal(report.validity,'Current');const preferred=Number(process.env.ICAX_REPAIR_SEGMENT||2661),selected=report.segments[preferred]?.status==='Collision'?preferred:report.segments.findIndex(s=>s.status==='Collision');assert.ok(selected>=0);
  const repairStart=Date.now(),task=await wait(await command('CamSimulation.AutoRepairCollision',{segmentIndex:String(selected),maxDeviationDeg:Number(process.env.ICAX_REPAIR_MAX_DEG||15),localPoseDeviationAuthorized:process.env.ICAX_REPAIR_LOCAL_AUTHORIZED==='1'}),'local-repair',240000),repairTaskMs=Date.now()-repairStart;write('repair-task.json',task);
  assert.equal(task.status,'Completed',task.error);const after=await read();write('after-report.json',setup(after).simulationCheck);assert.deepEqual(immutableCuts(after),immutableCuts(before));assert.deepEqual(after.workpieces,before.workpieces);assert.deepEqual(after.fixtures,before.fixtures);
  const repaired=task.result.status==='Repaired'&&task.result.changed;
  if(repaired){assert.equal(task.result.scope,'Local');assert.ok(Number(task.result.checkedSegmentCount)>0&&Number(task.result.checkedSegmentCount)<Number(task.result.totalSegmentCount));assert.equal(setup(after).simulationCheck.validity,'Current');
    if(process.env.ICAX_REPAIR_EXPECT_FIXED_KEYPOINTS==='1'){
      const search=task.result.poseSearch;assert.ok(search);const entry=Number(search.fixedEntryFrameIndex),exit=Number(search.fixedExitFrameIndex);assert.ok(entry<exit);assert.ok(Number(search.transportStepCount)<=Number(search.candidateCount)*(exit-entry));
      for(let i=0;i<report.frames.length;i++)if(i<=entry||i>=exit)assert.deepEqual(setup(after).simulationCheck.frames[i],report.frames[i],'Unrelated or boundary full pose changed at '+i);
      assert.deepEqual(setup(after).generatedGeometry.transitions.map(({inputKey,...route})=>route),setup(before).generatedGeometry.transitions.map(({inputKey,...route})=>route),'Cut repair must leave every rapid unchanged; resource input signatures may refresh');
    }
    const local=new Set(setup(after).simulationCheck.localCheckedSegmentIndices.map(Number));for(const i of local)assert.equal(setup(after).simulationCheck.segments[i].status,'Clear');assert.ok(local.has(selected));
    for(let i=0;i<report.segments.length;i++)if(!local.has(i))assert.deepEqual(setup(after).simulationCheck.segments[i],report.segments[i]);
    const afterPolicy=(await invoke('CamProcess.Read',{expected:after.expected,intentIds:[outer.intentId]})).items[0].posePolicy;for(const key of ['angularToleranceRad','leadDeviationLimitRad','sideDeviationLimitRad'])assert.equal(afterPolicy[key],policies.items[0].posePolicy[key]);for(const point of policies.items[0].posePolicy.keypoints){const next=afterPolicy.keypoints.find(p=>p.id===point.id);const edited=task.result.poseSearch?.kind==='Keypoint'&&task.result.adjustments.some(a=>JSON.stringify(a.sourceAnchor)===JSON.stringify(point.anchor));if(edited){assert.ok(next);assert.deepEqual(next.anchor,point.anchor);assert.equal(next.transition,point.transition);}else assert.deepEqual(next,point);}
    await rpc({action:'export-project',path:resolve(output,'repaired-local.i5cam')});
    assert.equal((await rpc({action:'undo'})).changed,true);const undone=await read();assert.deepEqual(actualGeometry(undone),actualGeometry(before));assert.deepEqual(setup(undone).simulationCheck,report);
    assert.equal((await rpc({action:'redo'})).changed,true);assert.deepEqual(setup(await read()).simulationCheck,setup(after).simulationCheck);
    await rpc({action:'save-reopen'});const reopened=await read();assert.deepEqual(actualGeometry(reopened),actualGeometry(after));assert.deepEqual(setup(reopened).simulationCheck,setup(after).simulationCheck);
  }else{assert.equal(task.result.status,'Unresolved');assert.equal(task.result.changed,false);assert.deepEqual(actualGeometry(after),actualGeometry(before));assert.deepEqual(setup(after).simulationCheck,report);assert.deepEqual(after.intentToolpaths,before.intentToolpaths);}
  const result={passed:true,repaired,source,project,selected,fullCheckMs:repairStart-checkStart,repairTaskMs,localRepairMs:Date.now()-repairStart,result:task.result};write('trace.json',result);console.log(JSON.stringify(result));
  }
}catch(error){write('failure.json',{message:error.message,stack:error.stack});throw error;}
finally{writeFileSync(resolve(output,'native.log'),logs.join('\n'));try{await rpc({action:'exit'});}finally{native.stdin.end();}}
