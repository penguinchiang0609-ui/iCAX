import test from 'node:test';
import assert from 'node:assert/strict';
import {simulationCommand,simulationField} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';
import {blockingProgress} from '../../apps/five-axis-cam/webpage/blockingProgress.mjs';

function deferred(){let resolve;const promise=new Promise(done=>{resolve=done;});return {promise,resolve};}
async function until(predicate){for(let i=0;i<100;++i){if(predicate())return;await new Promise(setImmediate);}assert.fail('head selection did not settle');}
async function browser(run){const previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};try{await run();}finally{globalThis.cancelAnimationFrame=previous;}}

function fixture({holdFirst=false,loseFirstReply=false,tool='simulation'}={}){
 let current=true,revision=1,nativeHead='original',lost=false;
 const gate=deferred(),receipts=new Map(),calls=[],events=[],s={cursor:0,frames:[],request:null};
 const setup={id:'setup',headModel:{kind:'Builtin',builtinDefinitionId:nativeHead},rapidSettings:{followHeightMm:1,collisionThresholdMm:.1}};
 const view={pending:false,fiveAxisArea:'toolpath',fiveAxisUI:{tools:{toolpath:tool}},fiveSimulation:s,scene:{domain:{expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',setups:[setup],placements:[],workpieces:[],fixtures:[],intentToolpaths:[]}}};
 const context={mount:{querySelector:()=>null},isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/tests/builtin-head.i5cam'},async autosave(){events.push({type:'save',head:nativeHead,revision,current});return {saved:true};}},sceneProxy:{async invoke(method,payload){
  if(method==='CamScene.ReadState'){events.push({type:'refresh',head:nativeHead,revision});return {...structuredClone(view.scene.domain),expected:{epoch:'epoch',mutationSequence:String(revision)},setups:[{...setup,headModel:{kind:'Builtin',builtinDefinitionId:nativeHead}}]};}
  assert.equal(method,'CamHead.Configure');calls.push(structuredClone(payload));events.push({type:'invoke',head:payload.builtinDefinitionId,commandId:payload.commandId});
  if(!receipts.has(payload.commandId)){
   assert.equal(payload.expected.mutationSequence,String(revision),'a new choice must use the refreshed expected revision');
   nativeHead=payload.builtinDefinitionId;++revision;receipts.set(payload.commandId,{changed:true});
  }
  if(holdFirst&&calls.length===1)await gate.promise;
  if(loseFirstReply&&!lost){lost=true;throw Error('The accepted native response was lost');}
  return receipts.get(payload.commandId);
 }}};
 const ops={renderProject(){events.push({type:'render',current});},appendProjectLog(){},async refreshSceneState(){assert.fail('head changes must not rebuild the scene');}};
 return {view,s,context,ops,calls,events,gate,choose:id=>simulationField(context,view,'builtin-head',id,ops),setCurrent:value=>{current=value;},saves:()=>events.filter(e=>e.type==='save'),head:()=>view.scene.domain.setups[0].headModel.builtinDefinitionId};
}

test('CAM rapid programming can select and save its head without opening trajectory simulation',()=>browser(async()=>{
 const f=fixture({tool:'rapid'});f.choose('LC-3015');await until(()=>!f.view.pending&&!f.s.busy&&f.saves().length===1);
 assert.equal(f.head(),'LC-3015');assert.equal(f.view.fiveAxisUI.tools.toolpath,'rapid');assert.equal(f.calls.length,1);
}));
test('a pending head choice stays inline and cannot trap the current selector focus in a modal',()=>browser(async()=>{
 const f=fixture({tool:'rapid',holdFirst:true});f.choose('LC-3015');assert.equal(f.view.pending,true);assert.equal(f.s.busy,true);assert.equal(blockingProgress(f.view),null);
 f.choose('TC-800');f.gate.resolve();await until(()=>!f.view.pending&&!f.s.busy);assert.equal(f.head(),'TC-800');
}));
test('builtin head changes serialize the latest choice after the held native receipt and refreshed revision',()=>browser(async()=>{
 const f=fixture({holdFirst:true});
 f.choose('LC-3015');assert.equal(f.calls.length,1);const firstId=f.calls[0].commandId;
 f.choose('IR-1600');f.choose('TC-800');f.choose('TC-800');
 assert.equal(f.calls.length,1,'subsequent changes must not run while the first receipt is pending');
 assert.equal(f.s.queuedBuiltinHeadId,'TC-800');assert.equal(f.saves().length,0);
 f.gate.resolve();await until(()=>!f.view.pending&&!f.s.busy&&f.calls.length===2);
 assert.deepEqual(f.calls.map(c=>c.builtinDefinitionId),['LC-3015','TC-800']);
 assert.notEqual(f.calls[1].commandId,firstId);assert.equal(f.calls[1].expected.mutationSequence,'2');
 assert.deepEqual(f.saves().map(e=>e.head),['LC-3015','TC-800'],'each acknowledged command saves once');
 assert.equal(f.events.filter(e=>e.type==='refresh').length,2);assert.equal(f.head(),'TC-800');assert.equal(f.s.request,null);
}));

test('an unknown builtin receipt blocks queued choices until the original command is retried unchanged',()=>browser(async()=>{
 const f=fixture({loseFirstReply:true});f.s.builtinHeadIdDraft='LC-3015';
 await simulationCommand(f.context,f.view,'head-builtin',f.ops);
 assert.equal(f.calls.length,1);assert.ok(f.s.request);const original=structuredClone(f.calls[0]);
 assert.equal(f.saves().length,0,'an unknown response cannot be acknowledged or saved');
 f.choose('IR-1600');f.choose('RC-1000');await new Promise(setImmediate);
 assert.equal(f.calls.length,1);assert.equal(f.s.queuedBuiltinHeadId,'RC-1000');
 await simulationCommand(f.context,f.view,'head-builtin',f.ops);
 assert.equal(f.calls.length,3);assert.deepEqual(f.calls[1],original,'retry preserves command ID, payload and original expected revision');
 assert.equal(f.calls[2].builtinDefinitionId,'RC-1000');assert.equal(f.calls[2].expected.mutationSequence,'2');
 assert.notEqual(f.calls[2].commandId,original.commandId);
 assert.deepEqual(f.saves().map(e=>e.head),['LC-3015','RC-1000']);assert.equal(f.head(),'RC-1000');assert.equal(f.s.request,null);assert.equal(f.view.pending,false);
}));

test('a receipt arriving after switching projects saves its captured project without applying the queued head',()=>browser(async()=>{
 const f=fixture({holdFirst:true});f.choose('LC-3015');f.choose('TC-800');f.setCurrent(false);f.gate.resolve();
 await until(()=>!f.view.pending&&!f.s.busy);
 assert.equal(f.calls.length,1);assert.deepEqual(f.saves().map(e=>e.head),['LC-3015']);
 assert.equal(f.events.filter(e=>e.type==='refresh').length,0);
 assert.equal(f.events.filter(e=>e.type==='render'&&!e.current).length,0);
 assert.equal(f.head(),'original','the hidden project view is not refreshed or given a late queued choice');
}));

test('an unrelated pending operation rejects builtin selections without silently queuing a save',()=>browser(async()=>{
 const f=fixture();f.view.pending=true;f.choose('LC-3015');await new Promise(setImmediate);
 assert.equal(f.calls.length,0);assert.equal(f.s.queuedBuiltinHeadId,undefined);assert.equal(f.s.builtinHeadIdDraft,undefined);assert.equal(f.view.pending,true);
}));
