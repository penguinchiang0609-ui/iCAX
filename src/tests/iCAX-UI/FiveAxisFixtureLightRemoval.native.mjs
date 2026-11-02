import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
const output=resolve('output/acceptance/remove-fixture-light-validation-20261102');mkdirSync(output,{recursive:true});
const runtime=resolve('src/x64/Debug'),child=spawn(resolve(runtime,'FiveAxisHeadInstanceTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
const pending=new Map(),logs=[];let serial=0;const fail=e=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(e);}pending.clear();};child.on('error',fail);child.on('exit',code=>{if(pending.size)fail(Error('Bridge exited '+code));});child.stderr.on('data',b=>logs.push(String(b)));
createInterface({input:child.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__'))return logs.push(line);const r=JSON.parse(line.slice(12)),p=pending.get(r.id);if(!p)return;pending.delete(r.id);clearTimeout(p.timer);r.ok?p.resolve(r.result):p.reject(Error(r.error));});
const rpc=m=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{pending.delete(id);reject(Error('Timeout '+m.action));},60000);pending.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,...m})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)});
const report={passed:false};
try{
 await rpc({action:'prepare-axis-test'});const before=await invoke('CamScene.ReadState');
 const error=async method=>{try{await invoke(method);throw Error('Unexpected method success');}catch(e){return e.message;}};
 report.removedCheck=await error('CamSimulation.CheckBeamClearance');report.removedRead=await error('CamSimulation.ReadBeamClearance');report.regularCollision=await error('CamSimulation.CheckCollision');
 assert.match(report.removedCheck,/SDO method not found/);assert.match(report.removedRead,/SDO method not found/);assert.doesNotMatch(report.regularCollision,/method not found/);assert.deepEqual(await invoke('CamScene.ReadState'),before,'Rejected removed commands must not change project data');report.passed=true;
}finally{writeFileSync(resolve(output,'native-removal.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'native-removal.log'),logs.join('\n'));try{await rpc({action:'exit'});}catch{}child.stdin.end();child.kill();}
console.log(JSON.stringify(report));
