import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,copyFileSync,writeFileSync,readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/pillar-generate-20261102/native'),runtime=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');mkdirSync(output,{recursive:true});
const source=process.env.ICAX_PILLAR_TEST_PROJECT||JSON.parse(readFileSync(resolve('output/acceptance/pillar-generate-20261102/before-fix-report.json'),'utf8')).project;
const project=resolve(output,'Pillar-'+Date.now()+'.i5cam');copyFileSync(resolve(source),project);
const native=spawn(resolve(runtime,'FiveAxisCAMTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:runtime,windowsHide:true,stdio:['pipe','pipe','pipe']});
let serial=0;const pending=new Map(),logs=[];
createInterface({input:native.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){logs.push(line);return;}const reply=JSON.parse(line.slice(12)),p=pending.get(reply.id);if(!p)return;pending.delete(reply.id);clearTimeout(p.timer);reply.ok?p.resolve(reply.result):p.reject(Error(reply.error));});native.stderr.on('data',d=>logs.push(String(d)));native.on('exit',code=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Native exited '+code));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{const id=++serial,timer=setTimeout(()=>{pending.delete(id);reject(Error('RPC timeout '+message.action));},180000);pending.set(id,{resolve,reject,timer});native.stdin.write(JSON.stringify({id,...message})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payloadText:serializeVariantText(payload)}),read=()=>invoke('CamScene.ReadState');
const write=(name,value)=>writeFileSync(resolve(output,name),JSON.stringify(value,null,2));
const hash=path=>createHash('sha256').update(readFileSync(path)).digest('hex');
const report={passed:false,project,source,sourceHash:hash(source),realNative:true,moduleHashes:Object.fromEntries(['FiveAxisCAM.dll','OpenCascadeResourceImport.dll'].map(name=>[name,hash(resolve(runtime,name))]))};
try{
 await rpc({action:'open-project',path:project});const before=await read();write('before.json',before);const setup=before.setups.find(s=>s.id===before.activeSetupId);report.beforeGeometry=setup.generatedGeometry?.status;assert.notEqual(report.beforeGeometry,'Current','Always recompute from the ungenerated incident archive');console.log(JSON.stringify({setupId:setup.id,cuts:before.intentToolpaths.length,geometry:report.beforeGeometry}));
 let task=await invoke('CamPlan.Generate',{commandId:crypto.randomUUID(),expected:before.expected,setupId:before.activeSetupId});while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke('CamPlan.QueryStatus',{epoch:task.epoch,taskId:task.taskId});}write('task.json',task);report.cuttingTask=task;console.log(JSON.stringify({status:task.status,error:task.error,result:task.result}));
 assert.equal(task.status,'Completed',task.error);assert.equal(Number(task.result.calculatedCount),21);let state=await read();const current=state.setups.find(s=>s.id===state.activeSetupId);report.cuttingOperations=current.generatedGeometry.operations.length;
 let rapid=await invoke('CamRapid.Generate',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,geometryRevision:current.generatedGeometry.revision,safe:false});while(rapid.status==='Running'){await new Promise(r=>setTimeout(r,100));rapid=await invoke('CamRapid.QueryStatus',{epoch:rapid.epoch,taskId:rapid.taskId});}write('rapid-task.json',rapid);report.rapidTask=rapid;console.log(JSON.stringify({status:rapid.status,error:rapid.error,count:rapid.result?.count,calculated:rapid.result?.calculatedCount,generated:rapid.result?.generatedCount}));assert.equal(rapid.status,'Completed',rapid.error);assert.equal(Number(rapid.result.calculatedCount),20);
 state=await read();write('after.json',state);const g=state.setups.find(s=>s.id===state.activeSetupId).generatedGeometry;report.rapidCount=g.transitions.length;report.generated=g.transitions.filter(t=>t.geometryStatus==='Generated').length;assert.equal(report.cuttingOperations,21);assert.equal(report.rapidCount,20);assert.equal(report.generated,20);report.passed=true;await rpc({action:'export-project',path:resolve(output,'Pillar-已生成.i5cam')});
}catch(error){report.error=error.stack;throw error;}finally{write('report.json',report);writeFileSync(resolve(output,'native.log'),logs.join('\n'));native.stdin.end();}
