import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
const runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'output/run/five-axis-cam-ab-compact-20261101'),output=resolve('output/acceptance/ab-compact-20261101');mkdirSync(output,{recursive:true});
const child=spawn(resolve(runtime,'FiveAxisHeadInstanceTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const pending=new Map(),log=[];let serial=0;const report={passed:false,realNativeSDO:true,checks:[]};
const fail=e=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(e);}pending.clear();};child.on('error',fail);child.on('exit',code=>{if(pending.size)fail(Error('Bridge exited '+code));});child.stderr.on('data',d=>log.push(String(d)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}const r=JSON.parse(line.slice(12)),p=pending.get(r.id);if(!p)return;pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{pending.delete(id);reject(Error('Native timeout'));},60000);pending.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
try{
 await rpc({action:'prepare-axis-test'});
 const imported=await invoke('MachineDefinition.Import',{sourcePath:resolve('src/apps/five-axis-cam/resources/machines/gantry-heads/GAB-3015/GAB-3015.urdf')});
 const created=(await invoke('Machine.Instantiate',{machineDefinitionId:imported.machineDefinitionId})).machine,id=created.id||created.entityId;
 const machines=(await invoke('Machine.List')).machines,m=machines.find(m=>(m.id||m.entityId)===id);assert.ok(m);
 const joints=Object.fromEntries(m.joints.map(j=>[j.jointName,j.entityId||j.id])),before=await invoke('CamScene.ReadState');
 assert.ok(joints.A&&joints.B);const degrees=v=>v*Math.PI/180;
 for(const a of [-46,0,50])for(const b of [-50,0,50]){
  const result=await invoke('CamSimulation.CheckMachineMove',{expected:before.expected,machineId:id,positions:{[joints.A]:degrees(a),[joints.B]:degrees(b)}});
  report.checks.push({A:a,B:b,status:result.collisionStatus,reason:result.reason||'',scope:result.collisionScope});
  assert.equal(result.collisionStatus,'Clear',JSON.stringify(report.checks.at(-1)));
 }
 for(const [axis,value] of [['A',50.01],['A',-46.01],['B',50.01],['B',-50.01]])await assert.rejects(invoke('CamSimulation.CheckMachineMove',{expected:before.expected,machineId:id,positions:{[joints[axis]]:degrees(value)}}),/LIMIT/);
 const after=await invoke('CamScene.ReadState');for(const key of ['workpieces','placements','intentToolpaths','headInstances'])assert.deepEqual(after[key],before[key]);report.readOnly=true;report.outOfRangeRejected=true;report.passed=true;
}catch(error){report.error=error.stack;throw error;}finally{writeFileSync(resolve(output,'endpoint-sweeps.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'endpoint-sweeps.log'),log.join('\n'));try{await rpc({action:'exit'});}catch{}child.stdin.end();child.kill();}
console.log(JSON.stringify(report));
