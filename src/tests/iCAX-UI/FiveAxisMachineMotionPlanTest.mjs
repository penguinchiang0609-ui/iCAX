import test from 'node:test';
import assert from 'node:assert/strict';
import {planMachineMotion,loadSavedMachinePlan,cancelMachinePlan,machinePlanningActivity} from '../../apps/five-axis-cam/webpage/machineMotionPlan.mjs';
import {encodeTaskProgress} from '../../apps/five-axis-cam/protocol/taskProgressPDOProtocol.mjs';
import {machineResourceTransport} from './fixtures/machineJSONResource.mjs';
const until=async predicate=>{for(let i=0;i<100;i++){if(predicate())return;await new Promise(resolve=>setImmediate(resolve));}throw Error('test stage was not reached');};
function fixture(){
 const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],nodes=[{id:'table',parent:-1,type:'fixed',axis:[0,0,1],origin:identity},{id:'head',parent:0,type:'prismatic',axis:[0,0,1],origin:identity}];
 const report={schema:'cam.machine-motion-plan',schemaVersion:'2',interpolation:'QuinticNormalized',status:'Ready',validity:'Current',playbackAllowed:true,modelKey:'machine',revision:{revision:'plan-1'},sampleCount:'2',durationSeconds:1,nodes,tool:1,support:0,frames:[{joints:[0,0],durationSeconds:0,laser:false},{joints:[0,1],durationSeconds:1,laser:true,motion:{interpolation:'QuinticNormalized',coefficients:[[0,0,0,0,0,0],[0,0,0,10,-15,6]]}}]};
 const transport=machineResourceTransport(),wire=body=>transport.wire(body,'planResourceRef');
 const setup={id:'setup',machineMotionPlanRef:{revision:'plan-1'},generatedGeometry:{status:'Current',operations:[{intentId:'cut'}],transitions:[]}},view={scene:{domain:{expected:{epoch:'e',mutationSequence:'1'},activeSetupId:'setup',setups:[setup],placements:[],workpieces:[],intentToolpaths:[]}}},native=structuredClone(view.scene.domain);
 const s=view.fiveMachine={snapshot:{modelKey:'machine'}},calls=[],context={mount:{querySelector:()=>null},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/motion.i5cam'},async autosave(){calls.push('save');}},sceneProxy:{resources:transport.resources,async invoke(method,request){calls.push({method,request});if(method==='CamScene.ReadState')return structuredClone(native);if(method==='CamSimulation.ReadMachinePlan'||method==='CamSimulation.ReadMachineAttempt')return wire(report);if(method==='CamSimulation.PlanMachine'&&report.playbackAllowed)native.expected.mutationSequence='2';return {status:'Completed',epoch:'e',taskId:'t',result:{setupId:"setup",status:report.status,playbackAllowed:report.playbackAllowed}};}}},ops={renderProject(){calls.push('render');},async refreshSceneState(){throw Error('planning must not refresh the full workspace');}};
 return {report,setup,view,s,calls,context,ops,wire,native};
}
test('planning consumes a native task and saves its exact accepted plan before exposing playback',async()=>{
 const f=fixture();await planMachineMotion(f.context,f.view,f.s,f.ops);
 const request=f.calls.find(c=>c.method==='CamSimulation.PlanMachine');assert.equal(request.request.setupId,'setup');assert.equal(request.request.expected.mutationSequence,'1');
 assert.ok(f.calls.indexOf('save')<f.calls.findLastIndex(c=>c.method==='CamScene.ReadState'));assert.deepEqual(f.s.report.frames,f.report.frames);assert.equal(f.s.revision,JSON.stringify(f.view.scene.domain.expected));assert.equal(f.s.planning,false);assert.equal(f.view.scene.domain.expected.mutationSequence,'2');
});
test('a rejected native plan cannot manufacture playback, a file edit or a passing report',async()=>{
 const f=fixture();Object.assign(f.report,{status:'Unresolved',playbackAllowed:false,frames:[],sampleCount:'0',reason:'No certified candidate'});
 await planMachineMotion(f.context,f.view,f.s,f.ops);assert.equal(f.calls.includes('save'),false);assert.equal(f.calls.includes('refresh'),false);assert.equal(f.s.report.playbackAllowed,false);assert.match(f.s.error,/No certified/);
});
test('save/reopen loads only a source-current matching machine plan and avoids duplicate body reads',async()=>{
 const f=fixture();await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);
 assert.deepEqual(f.s.report.frames,f.report.frames);assert.equal(f.calls.filter(c=>c.method==='CamSimulation.ReadMachinePlan').length,1);
 for(const report of [{validity:'Stale',playbackAllowed:false},{...f.report,modelKey:'other'}]){const g=fixture();Object.assign(g.report,report);await loadSavedMachinePlan(g.context,g.view,g.s,g.ops);assert.equal(g.s.report,undefined);}
});
test('selecting the current job restores a cleared playback report from the same native body',async()=>{
 const f=fixture();await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);const original=f.s.report;
 f.s.report=null;await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);
 assert.equal(f.s.report,original);assert.deepEqual(f.s.report.frames,f.report.frames);
 assert.equal(f.calls.filter(c=>c.method==='CamSimulation.ReadMachinePlan').length,1);
});
test('a snapshot that is not ready cannot consume the saved-plan adoption key',async()=>{
 const f=fixture();f.s.snapshot=null;await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);
 assert.equal(f.s.planLoadedKey,undefined);assert.equal(f.calls.length,0);
 f.s.snapshot={modelKey:'machine'};await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);
 assert.deepEqual(f.s.report.frames,f.report.frames);assert.ok(f.s.planLoadedKey);
});
test('a native body mismatched with the pending snapshot is adopted only when the matching snapshot arrives',async()=>{
 const f=fixture();f.s.snapshot={modelKey:'older-machine'};await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);
 assert.equal(f.s.report,undefined);assert.equal(f.s.planLoadedKey,undefined);
 f.s.snapshot={modelKey:'machine'};await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);
 assert.deepEqual(f.s.report.frames,f.report.frames);
 assert.equal(f.calls.filter(c=>c.method==='CamSimulation.ReadMachinePlan').length,1);
});
test('an in-flight old source cannot suppress the latest plan read or install its old report',async()=>{
 const f=fixture();let release;const pending=new Promise(resolve=>release=resolve);let reads=0;
 f.context.sceneProxy.invoke=async(method,request)=>{f.calls.push({method,request});return ++reads===1?pending:f.wire({...structuredClone(f.report),source:'latest'});};
 const old=loadSavedMachinePlan(f.context,f.view,f.s,f.ops);await Promise.resolve();
 f.setup.machineMotionPlanRef={revision:'plan-2'};f.view.scene.domain.expected.mutationSequence='2';
 await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);release(f.wire({...structuredClone(f.report),source:'old'}));await old;
 assert.equal(reads,2);assert.equal(f.s.report.source,'latest');assert.equal(f.s.planLoading,false);
});
test('a disposed project cannot install or retry a late native plan body',async()=>{
 const f=fixture();let release;f.context.sceneProxy.invoke=()=>new Promise(resolve=>release=resolve);
 const pending=loadSavedMachinePlan(f.context,f.view,f.s,f.ops);await Promise.resolve();f.view.disposed=true;
 f.setup.machineMotionPlanRef={revision:'plan-2'};release(f.wire(structuredClone(f.report)));await pending;
 assert.equal(f.s.report,undefined);assert.equal(f.s.planLoadedKey,undefined);assert.equal(f.s.planLoading,false);assert.equal(f.calls.length,0);
});
test('a failed read leaves the same current source available for a later retry',async()=>{
 const f=fixture();let reads=0;f.context.sceneProxy.invoke=async()=>{if(++reads===1)throw Error('temporary read failure');return f.wire(structuredClone(f.report));};
 await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);assert.equal(f.s.planLoadedKey,undefined);assert.equal(f.s.planReadKey,undefined);
 await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);assert.equal(reads,2);assert.deepEqual(f.s.report.frames,f.report.frames);
});
test('a saved current plan changing while its resource loads retries only the latest source',async()=>{
 const f=fixture(),get=f.context.sceneProxy.resources.get;let release,first=true;
 f.context.sceneProxy.resources.get=async(...args)=>{const response=await get(...args);if(first){first=false;return new Promise(resolve=>release=()=>resolve(response));}return response;};
 const pending=loadSavedMachinePlan(f.context,f.view,f.s,f.ops);await until(()=>release);
 f.setup.machineMotionPlanRef={revision:'plan-2'};f.view.scene.domain.expected.mutationSequence='2';f.report.source='latest';
 await loadSavedMachinePlan(f.context,f.view,f.s,f.ops);release();await pending;
 assert.equal(f.s.report.source,'latest');assert.equal(f.calls.filter(c=>c.method==='CamSimulation.ReadMachinePlan').length,2);
});
test('a completed plan is saved but cannot certify geometry changed during checkpoint and refresh',async()=>{
 const f=fixture(),invoke=f.context.sceneProxy.invoke;
 f.context.sceneProxy.invoke=async(method,request)=>method==='CamSimulation.ReadMachinePlan'?{validity:'Stale',playbackAllowed:false}:invoke(method,request);
 await assert.rejects(planMachineMotion(f.context,f.view,f.s,f.ops),/工程或运动计划已变化/);
 assert.equal(f.calls.filter(c=>c==='save').length,1);assert.equal(f.s.report,undefined);assert.equal(f.s.planning,false);
});
test('cancel during complete resource loading preserves the native commit save obligation without installing playback',async()=>{
 const f=fixture(),get=f.context.sceneProxy.resources.get;let release;
 f.context.sceneProxy.resources.get=async(...args)=>{const response=await get(...args);return new Promise(resolve=>release=()=>resolve(response));};
 const pending=planMachineMotion(f.context,f.view,f.s,f.ops);await until(()=>release);await cancelMachinePlan(f.context,f.s);release();await pending;
 assert.equal(f.calls.filter(c=>c==='save').length,1);assert.equal(f.s.report,undefined);assert.equal(f.s.planning,false);
});
test('real task cancellation is forwarded and terminal cancellation does not save',async()=>{
 const f=fixture();let queried=false,release;f.context.sceneProxy.pdo={enabled:true,async withReadDescriptor(_,reader){return reader(encodeTaskProgress({taskId:'t',epoch:'e',sequence:1,status:'Cancelled'}).buffer,{dataVersion:'1'});}};
 f.context.sceneProxy.invoke=async(method,request)=>{f.calls.push({method,request});if(method==='CamScene.ReadState')return structuredClone(f.native);if(method==='CamSimulation.PlanMachine')return new Promise(resolve=>release=()=>resolve({status:'Running',epoch:'e',taskId:'t',progressPDO:{id:'1'},machineProgress:{stage:'Solving'}}));if(method==='CamSimulation.QueryTask'){queried=true;return {status:'Cancelled',epoch:'e',taskId:'t'};}return {};};
 const running=planMachineMotion(f.context,f.view,f.s,f.ops);await until(()=>release);await cancelMachinePlan(f.context,f.s);release();await running;
 assert.equal(queried,true);assert.ok(f.calls.some(c=>c.method==='CamSimulation.CancelTask'&&c.request.taskId==='t'));assert.equal(f.calls.includes('save'),false);assert.equal(f.s.planning,false);assert.match(f.s.error,/取消/);
 assert.equal(machinePlanningActivity({planTask:{machineProgress:{stage:'CheckingMotion'}}}),'正在检查运动和避让');
});
test('native solver and selection progress identify the active stage and real station',()=>{
 assert.equal(machinePlanningActivity({planTask:{machineProgress:{stage:'Solving',currentStation:'0',totalStations:'5364'}}}),'正在求解关键姿态 1 / 5364');
 assert.equal(machinePlanningActivity({planTask:{machineProgress:{stage:'Solving',currentStation:'5363',totalStations:'5364'}}}),'正在求解关键姿态 5364 / 5364');
 assert.equal(machinePlanningActivity({planTask:{machineProgress:{stage:'Selecting',currentStation:'5364',totalStations:'5364'}}}),'正在选择连续轴运动');
 assert.equal(machinePlanningActivity({planTask:{machineProgress:{stage:'Refining',currentStation:'8',totalStations:'5364'}}}),'正在校验插值并补点 9 / 5364');
 assert.equal(machinePlanningActivity({planCancelRequested:true,planTask:{machineProgress:{stage:'Solving',currentStation:'3',totalStations:'9'}}}),'正在取消规划');
});

test('readback failure stops planning before any native plan is submitted',async()=>{
 const f=fixture(),invoke=f.context.sceneProxy.invoke;
 f.context.sceneProxy.invoke=(method,request)=>method==='CamScene.ReadState'?Promise.reject(Error('state read failed')):invoke(method,request);
 await assert.rejects(planMachineMotion(f.context,f.view,f.s,f.ops),/state read failed/);
 assert.equal(f.calls.some(c=>c.method==='CamSimulation.PlanMachine'),false);assert.equal(f.s.planning,false);
});

test('late initial readback cannot overwrite a new setup, source revision, proxy or refresh',async()=>{
 for(const change of [f=>f.view.scene.domain.activeSetupId='other',f=>f.view.scene.domain.expected.mutationSequence='99',f=>f.context.sceneProxy={},f=>f.view.sceneRefreshRequest=1]){
  const f=fixture(),scene=f.view.scene;let release;
  f.context.sceneProxy.invoke=()=>new Promise(resolve=>release=resolve);
  const running=planMachineMotion(f.context,f.view,f.s,f.ops);await until(()=>release);change(f);const expected=structuredClone(scene.domain);
  release(structuredClone(f.native));await running;
  assert.deepEqual(scene.domain,expected);assert.equal(f.s.report,undefined);assert.equal(f.s.planning,false);
 }
});

test('readback of another native setup is rejected instead of planning the old visible setup',async()=>{
 const f=fixture();f.native.activeSetupId='other';
 await assert.rejects(planMachineMotion(f.context,f.view,f.s,f.ops),/工程或装夹已变化/);
 assert.equal(f.calls.some(c=>c.method==='CamSimulation.PlanMachine'),false);assert.equal(f.view.scene.domain.activeSetupId,'setup');assert.equal(f.s.planning,false);
});

test('definitive native validation failures clear the old command; transport failures retain it for reconciliation',async()=>{
 for(const [reason,retained] of [['MOTION_AXIS_DYNAMICS_INVALID',false],['CAM_MACHINE_PLACEMENT_BELOW_TABLE',false],['connection closed',true]]){
  const f=fixture(),invoke=f.context.sceneProxy.invoke;
  f.context.sceneProxy.invoke=(method,request)=>method==='CamSimulation.PlanMachine'?Promise.reject(Error(reason)):invoke(method,request);
  await assert.rejects(planMachineMotion(f.context,f.view,f.s,f.ops),{message:reason});
  assert.equal(!!f.s.planRequest,retained);assert.equal(f.s.planning,false);assert.equal(f.s.report,undefined);assert.equal(f.calls.includes('save'),false);
 }
});
