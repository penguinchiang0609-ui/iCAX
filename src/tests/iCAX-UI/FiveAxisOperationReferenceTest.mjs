import test from 'node:test';
import assert from 'node:assert/strict';
import {canChangeOperationWorkpiece,operationCommand,operationField,renderOperationParameters} from '../../apps/five-axis-cam/webpage/operationWorkspace.mjs';
import {resetWorkpieceTools} from '../../apps/five-axis-cam/webpage/workpieceSelection.mjs';

const browser=run=>async()=>{const old=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};try{await run();}finally{if(old===undefined)delete globalThis.cancelAnimationFrame;else globalThis.cancelAnimationFrame=old;}};
const deferred=()=>{let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};};
function fixture({holdCommit=false,loseReceipt=false}={}){
 const native={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'s1',activePlacementId:'p1',workpieces:[{id:'w1',name:'甲<工件>',enabled:true,geometryRef:{url:'resource://cad-a',version:'1'}},{id:'w2',name:'乙工件',enabled:true,geometryRef:{url:'resource://cad-b',version:'2'}},{id:'off',name:'已禁用件',enabled:false}],setups:[{id:'s1',name:'工序一',machineType:{machineType:'HeadTable'},registrationPlan:{status:'Missing'},generatedGeometry:{status:'Missing',ordering:{mode:'Automatic'}}},{id:'s2',name:'工序二',machineType:{machineType:'HeadHead'}}],placements:[{id:'p1',setupId:'s1',workpieceId:'w1'},{id:'p2',setupId:'s2',workpieceId:'w1'}],fixtures:[],fixtureUnits:[],intentToolpaths:[]};
 const view={fiveAxisArea:'operation',scene:{activePlacementId:'p1',domain:structuredClone(native),intentToolpaths:[]},fiveAxisUI:{tools:{operation:'machine-type'}}},calls=[],events=[],gate=deferred(),receipts=new Map();let current=true,lost=false;
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[],ownerDocument:{querySelectorAll:()=>[]}},isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/tests/reference.i5cam'},async autosave(){events.push('save');return {saved:true};}},sceneProxy:{async invoke(method,payload){
  calls.push({method,payload:structuredClone(payload)});
  if(method==='CamScene.QueryCommand')return {status:'Running'};
  assert.equal(method,'CamOperation.Manage');assert.equal(payload.action,'reference');assert.equal(payload.setupId,'s1');assert.equal(Object.hasOwn(payload,'machineType'),false);
  if(!receipts.has(payload.commandId)){assert.deepEqual(payload.expected,native.expected);native.placements.find(p=>p.id==='p1').workpieceId=payload.workpieceId;native.expected.mutationSequence='2';receipts.set(payload.commandId,{setupId:'s1',placementId:'p1',workpieceId:payload.workpieceId});}
  if(holdCommit)await gate.promise;
  if(loseReceipt&&!lost){lost=true;throw Error('Transport acknowledgement lost');}
  return receipts.get(payload.commandId);
 }}};
 const ops={renderProject(){events.push('render');},appendProjectLog(){events.push('log');},async refreshSceneState(){Object.assign(view.scene.domain,structuredClone(native));view.scene.activePlacementId='p1';events.push('refresh');return true;},async fitViewAfterRenderPublish(){events.push('fit');}};
 return {view,domain:view.scene.domain,native,context,ops,calls,events,gate,commits:()=>receipts.size,setCurrent(value){current=value;},command:action=>operationCommand(context,view,action,ops),field:value=>operationField(context,view,'workpieceId',value,ops)};
}

test('operation parameters show plain reference text; no operation shows only the creation prompt',()=>{
 const f=fixture(),html=renderOperationParameters(f.view);assert.match(html,/operation-reference-value/);assert.match(html,/甲&lt;工件&gt;/);assert.doesNotMatch(html,/data-five-operation-field="workpieceId"/);assert.equal(canChangeOperationWorkpiece(f.view),true);
 f.domain.activeSetupId='';f.view.scene.activePlacementId='';const empty=renderOperationParameters(f.view);assert.match(empty,/新增工序/);assert.doesNotMatch(empty,/data-five-operation-field|reference-edit|operation-reference-value/);
});
test('reference editing and cancel remain local without rebinding the viewport or changing native state',browser(async()=>{
 const f=fixture(),before=structuredClone(f.native);await f.field('w2');assert.equal(f.view.fiveOperationReferenceEdit,undefined);
 await f.command('reference-edit');assert.match(renderOperationParameters(f.view),/data-five-operation-field="workpieceId"/);await f.field('w2');
 assert.equal(f.view.fiveOperationReferenceEdit.workpieceId,'w2');assert.equal(f.domain.placements[0].workpieceId,'w1');assert.equal(f.view.scene.activePlacementId,'p1');assert.deepEqual(f.native,before);assert.equal(f.calls.length,0);
 await f.command('reference-cancel');assert.equal(f.view.fiveOperationReferenceEdit,null);assert.deepEqual(f.native,before);assert.equal(f.calls.length,0);assert.doesNotMatch(renderOperationParameters(f.view),/data-five-operation-field="workpieceId"/);
}));
test('confirming a changed draft submits exactly one reference command and preserves CAD, the other operation and fixed type',browser(async()=>{
 const f=fixture(),cad=structuredClone(f.native.workpieces),other=structuredClone(f.native.setups[1]),type=structuredClone(f.native.setups[0].machineType);
 await f.command('reference-edit');await f.field('w2');const committing=f.command('reference-apply');await f.command('reference-apply');await committing;
 assert.equal(f.commits(),1);assert.equal(f.calls.length,1);assert.deepEqual(f.calls[0].payload.expected,{epoch:'epoch',mutationSequence:'1'});assert.equal(f.calls[0].payload.workpieceId,'w2');assert.equal(f.view.fiveOperationReferenceEdit,null);assert.equal(f.domain.placements[0].workpieceId,'w2');assert.equal(f.view.scene.activePlacementId,'p1');assert.deepEqual(f.native.workpieces,cad);assert.deepEqual(f.native.setups[1],other);assert.deepEqual(f.native.setups[0].machineType,type);assert.equal(f.events.filter(e=>e==='save').length,1);
}));
test('unchanged and disabled choices cannot publish a replacement, and a background read permits local edits and cancel',browser(async()=>{
 const f=fixture();await f.command('reference-edit');await f.command('reference-apply');assert.equal(f.calls.length,0);await f.field('off');assert.equal(f.view.fiveOperationReferenceEdit.workpieceId,'w1');
 f.view.pending=true;await f.field('w2');assert.equal(f.view.fiveOperationReferenceEdit.workpieceId,'w2');await f.command('reference-apply');assert.equal(f.calls.length,0);await f.command('reference-cancel');assert.equal(f.view.fiveOperationReferenceEdit,null);assert.equal(f.calls.length,0);
}));
const blockers=[
 ['locked',f=>{f.domain.setups[0].locked=true;}],
 ['machine binding',f=>{f.domain.setups[0].machineSetup={machineId:'machine'};}],
 ['saved fixture recipe',f=>{f.domain.setups[0].fixtureDesign={recipe:{thickness:3}};}],
 ['prepared registration',f=>{f.domain.setups[0].registrationPlan={status:'Prepared',points:[{modelPoint:[0,0,0]}]};}],
 ['stale registration',f=>{f.domain.setups[0].registrationPlan={status:'Stale'};}],
 ['generated toolpaths',f=>{f.domain.setups[0].generatedGeometry={status:'Stale',operations:[{intentId:'cut'}]};}],
 ['generated rapid',f=>{f.domain.setups[0].generatedGeometry={status:'Missing',transitions:[{from:'a',to:'b'}]};}],
 ['disabled cutting definition',f=>{f.domain.intentToolpaths=[{id:'cut',placementId:'p1',enabled:false}];}],
 ['disabled fixture',f=>{f.domain.fixtures=[{id:'fixture',setupId:'s1',enabled:false}];}],
 ['fixture unit',f=>{f.domain.fixtureUnits=[{id:'unit',setupId:'s1'}];}],
];
for(const [name,block] of blockers)test(`${name} removes the edit and confirmation eligibility without submitting an operation`,browser(async()=>{
 const f=fixture();await f.command('reference-edit');await f.field('w2');block(f);assert.equal(canChangeOperationWorkpiece(f.view),false);await f.command('reference-apply');assert.equal(f.calls.length,0);await f.command('reference-cancel');await f.command('reference-edit');assert.equal(f.view.fiveOperationReferenceEdit,null);assert.equal(f.calls.length,0);assert.doesNotMatch(renderOperationParameters(f.view),/data-five-command="five.operation.reference-edit"/);
}));
test('a reference draft cannot follow the user into a different operation or project epoch',browser(async()=>{
 for(const change of ['operation','epoch','project']){
  const f=fixture();await f.command('reference-edit');await f.field('w2');
  if(change==='operation'){f.domain.activeSetupId='s2';f.view.scene.activePlacementId='p2';}else if(change==='epoch')f.domain.expected.epoch='another';else f.setCurrent(false);
  await f.command('reference-apply');assert.equal(f.calls.length,0);assert.equal(f.native.placements[0].workpieceId,'w1');
 }
 const f=fixture();await f.command('reference-edit');resetWorkpieceTools(f.view);assert.equal(f.view.fiveOperationReferenceEdit,null);
}));
test('an unknown reference receipt keeps the original target and command for retry instead of writing another selected value',browser(async()=>{
 const f=fixture({loseReceipt:true});await f.command('reference-edit');await f.field('w2');await f.command('reference-apply');const original=structuredClone(f.calls[0]);assert.equal(f.commits(),1);assert.ok(f.view.fiveOperationRequest);await f.field('w1');await f.command('reference-cancel');assert.equal(f.view.fiveOperationReferenceEdit.workpieceId,'w2');f.domain.expected.mutationSequence='3';await f.command('retry');const mutations=f.calls.filter(c=>c.method==='CamOperation.Manage');assert.equal(mutations.length,2);assert.deepEqual(mutations[1],original);assert.equal(f.commits(),1);assert.equal(f.view.fiveOperationRequest,null);assert.equal(f.view.fiveOperationReferenceEdit,null);
}));
test('an unknown committed reference receipt retries the same payload after the target workpiece is disabled',browser(async()=>{
 const f=fixture({loseReceipt:true});await f.command('reference-edit');await f.field('w2');await f.command('reference-apply');const original=structuredClone(f.calls[0]);assert.equal(f.commits(),1);assert.ok(f.view.fiveOperationRequest);
 f.domain.workpieces.find(w=>w.id==='w2').enabled=false;f.native.workpieces.find(w=>w.id==='w2').enabled=false;f.domain.expected.mutationSequence='3';
 await f.command('retry');const mutations=f.calls.filter(c=>c.method==='CamOperation.Manage');assert.equal(mutations.length,2);assert.deepEqual(mutations[1],original);assert.equal(f.commits(),1);assert.equal(f.view.fiveOperationRequest,null);assert.equal(f.view.fiveOperationReferenceEdit,null);assert.equal(f.domain.placements[0].workpieceId,'w2');
}));
test('a late reference result cannot repaint, fit or clear another operation editor',browser(async()=>{
 const f=fixture({holdCommit:true});await f.command('reference-edit');await f.field('w2');const committing=f.command('reference-apply');f.domain.activeSetupId='s2';f.view.scene.activePlacementId='p2';const nextDraft={setupId:'s2',placementId:'p2',workpieceId:'w1'};f.view.fiveOperationReferenceEdit=nextDraft;f.gate.resolve();await committing;
 assert.equal(f.calls[0].payload.setupId,'s1');assert.equal(f.native.placements[0].workpieceId,'w2');assert.equal(f.domain.activeSetupId,'s2');assert.equal(f.view.scene.activePlacementId,'p2');assert.equal(f.view.fiveOperationReferenceEdit,nextDraft);assert.equal(f.events.includes('refresh'),false);assert.equal(f.events.includes('fit'),false);assert.equal(f.events.includes('log'),false);
}));
