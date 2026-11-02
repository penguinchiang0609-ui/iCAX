import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {cutPolicyDraft,processDraftDefaults} from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';
import {parsePathSamples} from '../../apps/five-axis-cam/webpage/pathSampleResource.mjs';

const output=resolve(process.env.ICAX_CREATE_PERF_OUTPUT||'output/tests/create-performance-20261101/baseline');mkdirSync(output,{recursive:true});
const native=resolve(process.env.ICAX_FIVE_AXIS_NATIVE_DIRECTORY||'src/x64/Debug');
const source=resolve(process.env.ICAX_CREATE_PERF_SOURCE||'C:/Users/pengu/Downloads/iCAX/汽车冲压件全流程-20261024/Video_HatRail_1200x220x70_t1p2.step');
const sourceHash=createHash('sha256').update(readFileSync(source)).digest('hex');
const nativeModuleHash=createHash('sha256').update(readFileSync(resolve(native,'FiveAxisCAM.dll'))).digest('hex');
const nativeKernelHash=createHash('sha256').update(readFileSync(resolve(native,'IntentToolpath.dll'))).digest('hex');
const processHandle=spawn(resolve(process.env.ICAX_FIVE_AXIS_TEST_EXECUTABLE||resolve(native,'FiveAxisCAMTest.exe')),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:native,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0,nativeExit=null;const pending=new Map(),calls=[],log=[],report={passed:false,source,native,nativeModuleHash,nativeKernelHash,stages:{},calls};
processHandle.stderr.on('data',b=>log.push(String(b)));
createInterface({input:processHandle.stdout}).on('line',line=>{if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}const response=JSON.parse(line.slice(12)),p=pending.get(response.id);if(!p)return;pending.delete(response.id);clearTimeout(p.timer);p.call.elapsedMs=performance.now()-p.start;if(response.ok)p.resolve(response.result);else p.reject(Error(response.error));});
processHandle.on('exit',code=>{nativeExit=code??-1;for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Bridge exited '+nativeExit+'\n'+log.slice(-15).join('\n')));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{if(nativeExit!==null)return reject(Error('Bridge exited '+nativeExit));const id=++sequence,call={method:message.method||message.action};calls.push(call);const timer=setTimeout(()=>{pending.delete(id);reject(Error('Timeout '+call.method));},600000);pending.set(id,{resolve,reject,timer,call,start:performance.now()});processHandle.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const wait=async(task,method)=>{task=await task;while(task.status==='Running'){await new Promise(r=>setTimeout(r,30));task=await invoke(method,{epoch:task.epoch,taskId:task.taskId});}assert.equal(task.status,'Completed',task.error);return task;};
const stage=async(name,fn)=>{console.log(name);const start=performance.now();try{return await fn();}finally{report.stages[name]=performance.now()-start;writeFileSync(resolve(output,'progress.json'),JSON.stringify(report,null,2));console.log(name+' '+Math.round(report.stages[name])+' ms');}};
try{
 await rpc({action:'info'});let state=await invoke('CamScene.ReadState');
 await stage('import',()=>wait(invokeTask('CamWorkpiece.Import',{commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId,sourcePath:source}),'CamWorkpiece.QueryTask'));
 state=await invoke('CamScene.ReadState');let placement=state.placements.find(p=>p.setupId===state.activeSetupId);
 if(!placement||!state.setups.some(s=>s.id===placement.setupId)){const setup=await invoke('CamSetup.Manage',{commandId:crypto.randomUUID(),expected:state.expected,placementId:(placement||state.placements[0]).id,action:'create',name:'性能测量'});state=await invoke('CamScene.ReadState');placement=state.placements.find(p=>p.setupId===state.activeSetupId);assert.ok(placement,JSON.stringify(setup));}
 const workpiecesBefore=structuredClone(state.workpieces),expected=state.expected,request=()=>({commandId:crypto.randomUUID(),expected,setupId:placement.setupId,placementId:placement.id});
 const auto=await stage('automaticFaces',async()=>wait(await invoke('CamCut.FindRetainedFaces',{...request(),observationDirection:[0,0,-1]}),'CamPlan.QueryStatus'));
 assert.deepEqual(Object.keys(auto.result).sort(),['selectedFaceCount','selectionId']);
 const [sources,selection]=await Promise.all([invoke('CamCut.QuerySources',request()),invoke('CamCut.ReadRetainedSelection',{epoch:auto.epoch,taskId:auto.taskId})]),ids=new Set(selection.faceIds.map(String));
 assert.deepEqual((await invoke('CamScene.ReadState')).workpieces,workpiecesBefore);
 const extraction={...request(),sourceGeometry:state.workpieces.find(w=>w.id===placement.workpieceId).geometryRef,extractionRuleRef:state.setups.find(s=>s.id===placement.setupId).planningProfileRef,
  selection:{mode:'Faces',faceUses:sources.faceUses.filter(f=>ids.has(String(f.localId)))},geometryOnly:true,representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1,observationDirection:[0,0,-1]},requestedKinds:['GeneralContour','Circle','Ellipse','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:'create-performance'};
 const extracted=await stage('extract',async()=>wait(await invoke('CamCut.ExtractCandidates',extraction),'CamPlan.QueryStatus'));
 const candidates=await invoke('CamCut.QueryCandidates',{epoch:extracted.epoch,candidateSet:extracted.result.candidateSet,pageSize:'100'});
 writeFileSync(resolve(output,'candidates.json'),JSON.stringify(candidates,null,2));
 const selected=candidates.candidates.filter(c=>c.confirmationEligibility==='Ready');report.faceCount=ids.size;report.candidateCount=selected.length;
 const policy=await stage('preparePolicy',()=>invoke('CamProcess.Prepare',{...request(),...cutPolicyDraft(processDraftDefaults)}));
 const createRequest={...request(),candidateSet:extracted.result.candidateSet,candidateIds:selected.map(c=>c.candidateId),draftDigest:extraction.draftDigest,decisions:[],process:policy.process,pose:policy.pose};
 const created=await stage('create',()=>invoke('CamCut.Create',createRequest));
 report.createdCount=created.createdIntentIds.length;
 await stage('definitionTask',()=>wait(Promise.resolve(created.definitionTask),'CamWorkpiece.QueryTask'));
 const projection=await stage('readDefinitions',()=>invoke('CamCut.Read',{expected:created.statePatch.expected,intentIds:created.createdIntentIds,projection:true}));
 const resource=await rpc({action:'resource',url:projection.sampleGeometryRef.url,version:projection.sampleGeometryRef.version,head:false});
 report.definitions=projection.items.map(item=>({id:item.id,name:item.name,kind:item.geometryKind,sourceState:item.sourceState,points:item.previewPointCount}));
 report.sampleBytes=resource.bytes.length;writeFileSync(resolve(output,'samples.bin'),Buffer.from(resource.bytes));
 const samples=Buffer.from(resource.bytes),decoded=parsePathSamples(samples.buffer.slice(samples.byteOffset,samples.byteOffset+samples.byteLength));
 writeFileSync(resolve(output,'definitions.json'),JSON.stringify(projection.items.map(item=>({...item,samples:decoded.get('definition:'+item.id)})),null,2));
 writeFileSync(resolve(output,'operations.json'),JSON.stringify(await rpc({action:'intent-operations'}),null,2));
 report.sourceHash=sourceHash;assert.equal(createHash('sha256').update(readFileSync(source)).digest('hex'),sourceHash);
 if(process.env.ICAX_CREATE_PERF_VERIFY==='1'){
  const receipt=await invoke('CamScene.QueryCommand',{epoch:createRequest.expected.epoch,commandId:createRequest.commandId});
  assert.equal(receipt.status,'Completed');assert.deepEqual(receipt.result,created);
  assert.deepEqual(await invoke('CamCut.Create',createRequest),created,'Retry must return the identical creation receipt');
  const readRequest={expected:created.statePatch.expected,intentIds:created.createdIntentIds,projection:true};
  const warm=await stage('warmRead',()=>invoke('CamCut.Read',readRequest));assert.deepEqual(warm.items,projection.items);
  await assert.rejects(()=>invoke('CamCut.Read',{...readRequest,intentIds:[created.createdIntentIds[0],created.createdIntentIds[0]]}),/CAM_INTENT_DUPLICATE/);
  const first=projection.items[0],renamed=await invoke('CamCut.Rename',{commandId:crypto.randomUUID(),expected:created.statePatch.expected,intentId:first.id,name:'批量读取刷新验证'});
  await assert.rejects(()=>invoke('CamCut.Read',readRequest),/CAM_REVISION_CONFLICT/);
  const refreshed=await invoke('CamCut.Read',{...readRequest,expected:renamed.statePatch.expected,intentIds:[created.createdIntentIds.at(-1),first.id]});
  assert.equal(refreshed.items[1].name,'批量读取刷新验证');assert.equal(refreshed.items[1].curveRef.revision,first.curveRef.revision);
  assert.equal(refreshed.items[0].id,created.createdIntentIds.at(-1));assert.equal(refreshed.statePatch.intentToolpaths.length,created.createdIntentIds.length);
  let current=await invoke('CamScene.ReadState');assert.deepEqual(current.workpieces,workpiecesBefore);
  if(process.env.ICAX_CREATE_PERF_VERIFY_EDIT==='1'){
   await invoke('CamCut.Reverse',{commandId:crypto.randomUUID(),expected:current.expected,intentIds:[first.id]});
   current=await invoke('CamScene.ReadState');
   for(const task of current.intentGenerationTasks||[])if(task.status==='Running')await wait(task,'CamWorkpiece.QueryTask');
   current=await invoke('CamScene.ReadState');
   const edited=current.intentToolpaths.find(c=>c.id===first.id);assert.equal(edited.sourceState,'Current');assert.equal(edited.reversed,!first.reversed);
   assert.equal(current.intentToolpaths.length,created.createdIntentIds.length);assert.deepEqual(current.workpieces,workpiecesBefore);
   report.editRegression={reverseRegenerated:true,pathIdentityRetained:true,cadUnchanged:true};
  }
  report.projectionRegression={idempotentReceipt:true,warmRead:true,duplicateRejected:true,oldRevisionRejected:true,renameRefreshed:true,requestedOrder:true,completeStatePatch:true,cadUnchanged:true};
 }
 assert.equal(report.createdCount,report.candidateCount);assert.ok(projection.items.every(c=>c.sourceState==='Current'));
 if(process.env.ICAX_CREATE_PERF_SAVE_REOPEN==='1'){
  const before=await invoke('CamScene.ReadState');
  report.project=resolve(output,'verified-cutting.i5cam');await rpc({action:'export-project',path:report.project});
  const reopened=await stage('saveReopen',()=>rpc({action:'save-reopen'}));
  for(const key of ['workpieces','placements','fixtures','intentToolpaths'])assert.deepEqual(reopened[key],before[key],key);
  const definitions=await invoke('CamCut.Read',{expected:reopened.expected,intentIds:created.createdIntentIds,projection:true});
  assert.deepEqual(definitions.items,projection.items);report.persistence={closedAndReopened:true,cutCount:definitions.items.length,cadUnchanged:true};
 }
 assert.equal(createHash('sha256').update(readFileSync(resolve(native,'FiveAxisCAM.dll'))).digest('hex'),nativeModuleHash,'Native binary changed during measurement');
 assert.equal(createHash('sha256').update(readFileSync(resolve(native,'IntentToolpath.dll'))).digest('hex'),nativeKernelHash,'Native kernel changed during measurement');
 report.passed=true;
}catch(error){report.failure=error.stack;throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'native.log'),log.join('\n'));await rpc({action:'exit'}).catch(()=>{});processHandle.stdin.end();console.log(JSON.stringify({passed:report.passed,stages:report.stages,cuts:report.createdCount,failure:report.failure}));}

async function invokeTask(method,payload){return invoke(method,payload);}
