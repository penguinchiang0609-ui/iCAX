import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';

const native=resolve(process.env.ICAX_FIXTURE_PERF_RUNTIME||'output/run/five-axis-cam-fixture-receipt-20261102');
const executable=process.env.ICAX_FIXTURE_PERF_EXECUTABLE||'FiveAxisCAMKeyposeFinalTest.exe';
const output=resolve(process.env.ICAX_FIXTURE_PERF_OUTPUT||'output/acceptance/fixture-performance-20261102/current');mkdirSync(output,{recursive:true});
const evidence=JSON.parse(readFileSync(resolve('output/acceptance/fixture-performance-20261102/live-state.json')));
const source=resolve('TestData/FA/汽车/04_Pillar_Reinforcement_900x300_t1p4.step');
const hash=path=>createHash('sha256').update(readFileSync(path)).digest('hex');
const report={passed:false,sourceHash:hash(source),modules:Object.fromEntries([executable,'FiveAxisCAM.dll','OpenCascadeResourceImport.dll','IntentToolpath.dll'].map(name=>[name,hash(resolve(native,name))])),stages:{},calls:[]};
const child=spawn(resolve(native,executable),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:native,windowsHide:true,env:{...process.env,ICAX_FIXTURE_TRACE:'1'},stdio:['pipe','pipe','pipe']});
let sequence=0,exited=false;const pending=new Map(),log=[];
child.stderr.on('data',b=>{log.push(String(b));writeFileSync(resolve(output,'trace.log'),log.join(''));});
createInterface({input:child.stdout}).on('line',line=>{
 if(!line.startsWith('__FIVE_RPC__')){log.push(line+'\n');return;}
 const response=JSON.parse(line.slice(12)),p=pending.get(response.id);if(!p)return;
 pending.delete(response.id);clearTimeout(p.timer);p.call.elapsedMs=performance.now()-p.start;
 response.ok?p.resolve(response.result):p.reject(Error(response.error));
});
child.on('exit',code=>{exited=true;for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Bridge exited '+code));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{
 if(exited)return reject(Error('Bridge already exited'));
 const id=++sequence,call={method:message.method||message.action};report.calls.push(call);
 const timer=setTimeout(()=>{pending.delete(id);reject(Error('Timeout '+call.method));},600000);
 pending.set(id,{resolve,reject,timer,call,start:performance.now()});
 child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');
});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const wait=async(task,method)=>{while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke(method,{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
const stage=async(name,fn)=>{console.log(name);const start=performance.now();try{return await fn();}finally{report.stages[name]=performance.now()-start;writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));}};
let state;const read=async()=>state=await invoke('CamScene.ReadState');
const request=()=>({commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId});
try{
 await rpc({action:'info'});await read();
 await stage('import',async()=>wait(await invoke('CamWorkpiece.Import',{...request(),sourcePath:source}),'CamWorkpiece.QueryTask'));await read();
 if(!state.setups.some(s=>s.id===state.activeSetupId)){await invoke('CamSetup.Manage',{commandId:crypto.randomUUID(),expected:state.expected,placementId:state.placements[0].id,action:'create',name:'支撑耗时复现'});await read();}
 const placement=state.placements.find(p=>p.setupId===state.activeSetupId);
 await invoke('CamPlacement.SetTransform',{...request(),placementId:placement.id,transform:evidence.state.placements[0].transform});await read();
 const before=structuredClone(state),recipe=structuredClone(evidence.units[0].recipe);
 for(const key of ['id','baseId','nextNumber','supportFaceCount'])delete recipe[key];recipe.stations=[];
 const started=performance.now();
 const task=await stage('generateRequest',()=>invoke('CamFixture.Generate',{...request(),recipe,newUnit:true}));
 const completed=await stage('workerAndPublication',()=>wait(task,'CamFixture.QueryTask'));
 const scope={epoch:completed.epoch,taskId:completed.taskId,setupId:state.activeSetupId};
 const design=await stage('readDesign',()=>invoke('CamFixture.ReadDesign',scope));
 const parts=await stage('queryParts',()=>invoke('CamFixture.QueryParts',{...scope,snapshot:design.snapshot,list:'candidate'}));
 report.design=design;report.parts=parts.items;
 await stage('readMeshes',()=>Promise.all(parts.items.map(async part=>{const resource=await rpc({action:'resource',url:part.previewRef.url,version:String(part.previewRef.version),encoding:'base64',head:false});const bytes=Buffer.from(resource.base64,'base64');return {id:part.partId,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};}))).then(value=>report.meshes=value);
 report.totalGenerationMs=performance.now()-started;
 const geometry=[];
 for(const part of parts.items){const detail=await invoke('CamFixture.PartDetails',{...scope,snapshot:parts.snapshot,list:'candidate',partId:part.partId});geometry.push(detail);}
 report.details=geometry;await read();
 for(const key of ['workpieces','placements','fixtures','intentToolpaths'])assert.deepEqual(state[key],before[key],key);
 assert.equal(parts.items.length,8);assert.equal(design.candidate.cutClearance.status,'Unchecked');
 if(process.env.ICAX_FIXTURE_PERF_ACCEPTANCE==='1'){
  await stage('confirm',()=>invoke('CamFixture.Confirm',{...request(),taskId:completed.taskId,candidateId:design.candidate.id,supportReviewed:true}));await read();
  assert.equal(state.fixtures.length,8);assert.equal(state.fixtureUnits.length,1);
  const cad=items=>items.map(item=>{const value=structuredClone(item);delete value.previewRef;delete value.importMetadata.displayMesh;return value;});
  assert.deepEqual(cad(state.workpieces),cad(before.workpieces),'source CAD');
  for(const key of ['placements','intentToolpaths'])assert.deepEqual(state[key],before[key],key);
  const confirmed=structuredClone(state),project=resolve(output,'exact-pillar-support.i5cam');
  await stage('save',()=>rpc({action:'export-project',path:project}));
  const reopened=await stage('reopen',()=>rpc({action:'save-reopen'}));
  for(const key of ['workpieces','placements','fixtures','intentToolpaths'])assert.deepEqual(reopened[key],confirmed[key],key);
  assert.equal(reopened.fixtureUnits.length,1);
  report.acceptance={confirmedParts:8,savedAndReopened:true,cadUnchanged:true,project,projectHash:hash(project)};
  await read();const unitId=state.fixtureUnits[0].id;
  await stage('breakdown',()=>invoke('CamFixture.Breakdown',{...request(),unitId}));await read();
  const delivery=await stage('exportDXF',()=>invoke('CamFixture.Export',{...request(),unitId,destination:output}));
  const dxfFiles=delivery.files.filter(file=>file.name.endsWith('.dxf'));assert.equal(dxfFiles.length,8);
  for(const file of delivery.files)assert.equal(hash(resolve(delivery.directory,file.name)),file.sha256,file.name);
  report.acceptance.delivery=delivery;
 }
 assert.equal(hash(source),report.sourceHash);
 report.passed=true;
}catch(error){report.failure=error.stack;throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'trace.log'),log.join(''));await rpc({action:'exit'}).catch(()=>{});child.stdin.end();console.log(JSON.stringify({passed:report.passed,stages:report.stages,totalGenerationMs:report.totalGenerationMs,failure:report.failure}));}
