import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {serializeVariantText} from '../../iCAX-UI/SDK/SDO/variantSerializer.mjs';
import {cutPolicyDraft,processDraftDefaults} from '../../apps/five-axis-cam/webpage/processPolicyDraft.mjs';

const output=resolve(process.env.ICAX_RECEIPT_OUTPUT||'output/acceptance/fixture-cut-receipt-20261102/native');
mkdirSync(output,{recursive:true});
const native=resolve('src/x64/Debug'),source=resolve('TestData/FA/汽车/04_Pillar_Reinforcement_900x300_t1p4.step');
const hash=path=>createHash('sha256').update(readFileSync(path)).digest('hex');
const report={passed:false,sourceHash:hash(source),moduleHash:hash(resolve(native,'FiveAxisCAM.dll')),stages:{}};
const child=spawn(resolve(native,'FiveAxisCAMKeyposeFinalTest.exe'),['--gtest_filter=FiveAxisBridge.DISABLED_Serve','--gtest_also_run_disabled_tests'],{cwd:native,windowsHide:true,stdio:['pipe','pipe','pipe']});
let sequence=0,exited=false;const pending=new Map(),log=[];
child.stderr.on('data',b=>log.push(String(b)));
createInterface({input:child.stdout}).on('line',line=>{
 if(!line.startsWith('__FIVE_RPC__')){log.push(line);return;}
 const response=JSON.parse(line.slice(12)),p=pending.get(response.id);if(!p)return;
 pending.delete(response.id);clearTimeout(p.timer);response.ok?p.resolve(response.result):p.reject(Error(response.error));
});
child.on('exit',code=>{exited=true;for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Native bridge exited '+code));}pending.clear();});
const rpc=message=>new Promise((resolve,reject)=>{
 if(exited)return reject(Error('Native bridge has exited'));
 const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(Error('Timeout '+(message.method||message.action)));},600000);
 pending.set(id,{resolve,reject,timer});
 child.stdin.write(JSON.stringify({id,...message,...(message.action==='invoke'?{payload:undefined,payloadText:serializeVariantText(message.payload||{})}:{})})+'\n');
});
const invoke=(method,payload={})=>rpc({action:'invoke',scope:'scene',method,payload});
const wait=async(task,method='CamWorkpiece.QueryTask')=>{
 while(task.status==='Running'){await new Promise(r=>setTimeout(r,100));task=await invoke(method,{epoch:task.epoch,taskId:task.taskId});}
 assert.equal(task.status,'Completed',task.error);return task;
};
const stage=async(name,fn)=>{console.log(name);const start=performance.now();try{return await fn();}finally{report.stages[name]=Math.round(performance.now()-start);writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));}};
let state;
const read=async()=>state=await invoke('CamScene.ReadState');
const request=()=>({commandId:crypto.randomUUID(),expected:state.expected,setupId:state.activeSetupId});
try{
 await rpc({action:'info'});await read();
 await stage('import',async()=>wait(await invoke('CamWorkpiece.Import',{...request(),sourcePath:source})));await read();
 if(!state.activeSetupId||!state.setups.some(s=>s.id===state.activeSetupId)){
  await invoke('CamSetup.Manage',{commandId:crypto.randomUUID(),expected:state.expected,placementId:state.placements[0].id,action:'create',name:'夹具后添加刀路'});await read();
 }
 const placement=state.placements.find(p=>p.setupId===state.activeSetupId);
 const recipe={supportFaces:['1'],stations:[],layout:'Automatic',pattern:'Skeleton',layoutAxis:'X',supportDirection:[0,0,1],supportCount:5,connectionCount:2,supportPositions:[],connectionPositions:[],baseShape:'Rectangle',sizing:'Automatic',areaWidth:240,areaDepth:240,areaX:0,areaY:0,postWidth:12,family:'ParallelX',material:'CarbonSteel',thickness:3,baseThickness:5,baseDrop:40,margin:15,gap:.2,minimumWeb:5,jointMode:'TabSlot',jointClearance:.2,tenonWidth:8,tenonPitch:60,jointWeb:1.5,tenonRadius:1,windowMode:'Automatic',windowWeb:10,openingReliefDepth:5,cutClearanceMode:'None',cutClearanceMm:2,beamRadiusMm:.5};
 // The observed support is the opposite skin; face 1 is the cutting skin.
 recipe.supportFaces=['66'];
 const fixture=await stage('fixture',async()=>wait(await invoke('CamFixture.Generate',{...request(),recipe,newUnit:true}),'CamFixture.QueryTask'));
 const design=await invoke('CamFixture.ReadDesign',{setupId:state.activeSetupId,epoch:fixture.epoch,taskId:fixture.taskId});
 await invoke('CamFixture.Confirm',{...request(),taskId:fixture.taskId,candidateId:design.candidate.id,supportReviewed:true});await read();
 assert.equal(state.fixtures.length,8);assert.equal(state.intentToolpaths.length,0);
 const clearance=state.fixtureUnits[0].design.assembly.cutClearance;
 assert.equal(clearance.mode,'None');assert.equal(clearance.status,'Unchecked');assert(!Object.hasOwn(clearance,'validity'));
 const before=structuredClone(state);await rpc({action:'export-project',path:resolve(output,'before-cuts.i5cam')});
 const sources=await invoke('CamCut.QuerySources',{expected:state.expected,setupId:state.activeSetupId,placementId:placement.id});
 const extraction={...request(),placementId:placement.id,sourceGeometry:sources.sourceGeometry,extractionRuleRef:sources.extractionRuleRef,selection:{mode:'Faces',faceUses:sources.faceUses.filter(f=>String(f.localId)==='1')},geometryOnly:true,representation:'SkinSurface',workpieceFamily:'SheetSurface',side:{outwardSign:1,observationDirection:[0,0,-1]},requestedKinds:['GeneralContour','Circle','Ellipse','Rectangle','Slot'],draftId:crypto.randomUUID(),draftRevision:'1',draftDigest:'fixture-cut-receipt'};
 const extracted=await stage('extract',async()=>wait(await invoke('CamCut.ExtractCandidates',extraction),'CamPlan.QueryStatus'));
 const candidates=await invoke('CamCut.QueryCandidates',{epoch:extracted.epoch,candidateSet:extracted.result.candidateSet,pageSize:'100'});
 const selected=candidates.candidates.filter(c=>c.confirmationEligibility==='Ready');assert.equal(selected.length,21);
 const policy=await invoke('CamProcess.Prepare',{...request(),placementId:placement.id,...cutPolicyDraft(processDraftDefaults)});
 const createRequest={...request(),placementId:placement.id,candidateSet:extracted.result.candidateSet,candidateIds:selected.map(c=>c.candidateId),draftDigest:extraction.draftDigest,decisions:[],process:policy.process,pose:policy.pose};
 if(process.env.ICAX_RECEIPT_EXPECT_FAILURE==='1'){
  await assert.rejects(()=>invoke('CamCut.Create',createRequest),/invalid map<K, T> key/);await read();
  assert.equal(state.intentToolpaths.length,21);assert.notDeepEqual(state.expected,before.expected);
  const heads=await invoke('CamHead.ListBuiltins');assert(heads.items.length);
  await assert.rejects(()=>invoke('CamHead.Configure',{...request(),expected:before.expected,kind:'Builtin',builtinDefinitionId:heads.items[0].id}),/CAM_REVISION_CONFLICT/);
  report.reproduced={createdInBackend:21,creationReplyFailed:true,headSelectionRejectedUsingUnupdatedRevision:true};
 }else{
  const created=await stage('create',()=>invoke('CamCut.Create',createRequest));assert.equal(created.createdIntentIds.length,21);
  assert.deepEqual(await invoke('CamCut.Create',createRequest),created);
  const receipt=await invoke('CamScene.QueryCommand',{epoch:createRequest.expected.epoch,commandId:createRequest.commandId});assert.equal(receipt.status,'Completed');assert.deepEqual(receipt.result,created);
  await wait(created.definitionTask);await read();assert.deepEqual(created.statePatch.expected,state.expected);
  const projected=await invoke('CamCut.Read',{expected:state.expected,intentIds:created.createdIntentIds,projection:true});assert.equal(projected.items.length,21);
  const compact=projected.statePatch.fixtureValidity.units[state.fixtureUnits[0].id].assembly.cutClearance;
  assert.deepEqual(compact,{status:'Unchecked'});
  const cutsBeforeHead=structuredClone(state.intentToolpaths),heads=await invoke('CamHead.ListBuiltins');assert(heads.items.length);
  await stage('selectHead',()=>invoke('CamHead.Configure',{...request(),kind:'Builtin',builtinDefinitionId:heads.items[0].id}));await read();
  assert.equal(state.setups[0].headModel.builtinDefinitionId,heads.items[0].id);assert.deepEqual(state.intentToolpaths,cutsBeforeHead);
  for(const key of ['workpieces','placements','fixtures'])assert.deepEqual(state[key],before[key],key);
  await rpc({action:'export-project',path:resolve(output,'after-head.i5cam')});
  const reopened=await rpc({action:'save-reopen'});
  for(const key of ['workpieces','placements','fixtures','intentToolpaths','headInstances'])assert.deepEqual(reopened[key],state[key],key);
  const reread=await invoke('CamCut.Read',{expected:reopened.expected,intentIds:created.createdIntentIds,projection:true});assert.equal(reread.items.length,21);
  report.verified={cuts:21,fixtureParts:8,idempotentCreation:true,currentReceipt:true,headSelection:true,cadAndPathsUnchanged:true,savedAndReopened:true};
 }
 assert.equal(hash(source),report.sourceHash);assert.equal(hash(resolve(native,'FiveAxisCAM.dll')),report.moduleHash);report.passed=true;
}catch(error){report.failure=error.stack;throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));writeFileSync(resolve(output,'native.log'),log.join('\n'));await rpc({action:'exit'}).catch(()=>{});child.stdin.end();console.log(JSON.stringify(report));}
