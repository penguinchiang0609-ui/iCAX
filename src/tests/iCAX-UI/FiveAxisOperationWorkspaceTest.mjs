import test from 'node:test';
import assert from 'node:assert/strict';
import {AREAS,TOOLS,geometryEditing,selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {canEnterArea,camPreparationReason} from '../../apps/five-axis-cam/webpage/workflow.mjs';
import {operationPlacements,operationField,operationCommand,renderOperationList,renderOperationParameters,syncMachineTypeAssignments} from '../../apps/five-axis-cam/webpage/operationWorkspace.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const response=(source='Workpiece',machineType='HeadHead')=>({scopes:{Setup:{mode:'Inherit'},Workpiece:{mode:'Assigned',machineType:'HeadHead'},Project:{mode:'Inherit'},Product:{mode:'Assigned',machineType:'HeadTable'}},effective:{source,machineType},productSnapshot:{mode:'Inherit'},productPreferencesRevision:'preferences-4'});
const deferred=()=>{let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};};
async function until(predicate){for(let index=0;index<100;index++){if(predicate())return;await new Promise(setImmediate);}assert.fail('Operation query did not settle');}
function fixture({invoke}={}){
 const calls=[],events=[],domain={jobId:'job',expected:{epoch:'epoch',mutationSequence:'7'},activeSetupId:'s1',activePlacementId:'p1',workpieces:[{id:'w1',name:'甲<件>',geometryRef:{url:'resource://part1'}},{id:'w2',name:'乙件',geometryRef:{url:'resource://part2'}}],setups:[{id:'s1',name:'工序一',enabled:true},{id:'s2',name:'工序二',enabled:true}],placements:[{id:'source-w1',workpieceId:'w1',transform:identity},{id:'p1',setupId:'s1',workpieceId:'w1',transform:identity},{id:'p2',setupId:'s2',workpieceId:'w2',transform:identity}],fixtures:[],fixtureUnits:[]};
 const view={fiveAxisArea:'operation',fivePreparationKind:'setup',scene:{activePlacementId:'p1',domain,intentToolpaths:[]}};selectTool(view,'machine-type');
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[],ownerDocument:{querySelectorAll:()=>[]}},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/operation.i5cam'},async autosave(){events.push('save');return {saved:true};}},sceneProxy:{async invoke(method,payload){calls.push({method,payload:structuredClone(payload)});if(invoke)return invoke(method,payload);if(method==='CamOperation.MachineAssignments')return response();throw Error('Unexpected method '+method);}}};
 const ops={renderProject(){events.push('render');},appendProjectLog(){events.push('log');},async refreshSceneState(){events.push('refresh');}};
 return {view,context,ops,calls,events,domain};
}

test('navigation separates shared workpiece geometry from operation placement without adding a navigation gate',()=>{
 assert.deepEqual(AREAS.map(area=>[area.id,area.title]),[['workpiece','工件'],['operation','工序'],['toolpath','刀路'],['simulation','孪生'],['help','帮助']]);
 for(const tool of ['scale','align'])assert.equal(TOOLS[tool].area,'workpiece');
 for(const tool of ['translate','rotate','fixture','registration','setup','machine-type'])assert.equal(TOOLS[tool].area,'operation');
 assert.equal(geometryEditing({fiveAxisArea:'operation',fivePreparationKind:'workpiece'}),false);
 assert.equal(geometryEditing({fiveAxisArea:'workpiece',fivePreparationKind:'workpiece'}),true);
 for(const area of AREAS)assert.equal(canEnterArea({},area.id),true);
 const f=fixture();f.domain.setups=[];assert.match(camPreparationReason(f.view),/工序/);
});

test('operation rows belong to the current workpiece and escape source names',()=>{
 const f=fixture(),before=structuredClone(f.domain);
 assert.deepEqual(operationPlacements(f.view).map(p=>p.id),['p1','p2']);
 const html=renderOperationList(f.view);assert.match(html,/data-five-key="operation-list"/);assert.match(html,/five.setup.activate:p1/);assert.doesNotMatch(html,/five.setup.activate:p2/);assert.match(html,/甲&lt;件&gt;/);assert.doesNotMatch(html,/five.setup.activate:source-w1/);
 assert.deepEqual(f.domain,before);
});

test('the operation inspector exposes the fixed machine type without a second selector or scope editor',async()=>{
 const f=fixture();syncMachineTypeAssignments(f.context,f.view,f.ops);await until(()=>!f.view.fiveMachineTypeAssignments.loading);
 const before=structuredClone(f.domain);await operationField(f.context,f.view,'machineType','TableTable',f.ops);await operationField(f.context,f.view,'machineScope','Project',f.ops);
 const html=renderOperationParameters(f.view);assert.match(html,/data-five-operation-machine-effective/);assert.match(html,/实际使用/);assert.match(html,/双摆头/);assert.match(html,/机型在新增工序时确定/);assert.doesNotMatch(html,/operation-field-machineType|machineScope|设为本工程默认|machine-set|machine-inherit/);assert.deepEqual(f.domain,before);
 assert.equal(f.calls.some(call=>call.method==='CamOperation.AssignMachineType'),false);
});

test('an assignment query for a previous operation cannot overwrite the newly selected operation',async()=>{
 const gates=new Map(),f=fixture({invoke(method,payload){assert.equal(method,'CamOperation.MachineAssignments');const gate=deferred();gates.set(payload.setupId,gate);return gate.promise;}});
 syncMachineTypeAssignments(f.context,f.view,f.ops);assert.ok(gates.has('s1'));
 f.domain.activeSetupId='s2';f.domain.activePlacementId='p2';f.view.scene.activePlacementId='p2';syncMachineTypeAssignments(f.context,f.view,f.ops);assert.ok(gates.has('s2'));
 gates.get('s2').resolve(response('Setup','TableTable'));await until(()=>f.view.fiveMachineTypeAssignments.data?.effective.machineType==='TableTable');
 gates.get('s1').resolve(response('Project','HeadTable'));await new Promise(setImmediate);
 assert.equal(f.view.fiveMachineTypeAssignments.setupId,'s2');assert.equal(f.view.fiveMachineTypeAssignments.data.effective.machineType,'TableTable');
});

test('obsolete machine setting commands cannot change or restore the type of an existing operation',async()=>{
 const f=fixture();
 syncMachineTypeAssignments(f.context,f.view,f.ops);await until(()=>!f.view.fiveMachineTypeAssignments.loading);
 await operationField(f.context,f.view,'machineScope','Product',f.ops);await operationField(f.context,f.view,'machineType','TableTable',f.ops);const before=structuredClone(f.domain);
 await operationCommand(f.context,f.view,'machine-set',f.ops);
 await operationCommand(f.context,f.view,'machine-inherit',f.ops);
 assert.equal(f.calls.some(call=>call.method==='CamOperation.AssignMachineType'),false);
 assert.deepEqual(f.domain,before);assert.equal(f.events.includes('save'),false);assert.equal(f.events.includes('refresh'),false);
});

test('a stale create response retains its workpiece context and machine choice for an identical retry',async()=>{
 let rejectFirst=true;const f=fixture({invoke(method,payload){
  if(method==='CamOperation.Manage'){if(rejectFirst){rejectFirst=false;throw Error('Lost response');}return {setupId:'s3',placementId:'p3'};}
  if(method==='CamScene.QueryCommand')return {status:'Running'};
  throw Error('Unexpected '+method);
 }});
 await operationCommand(f.context,f.view,'create',f.ops);
 await operationField(f.context,f.view,'workpieceId','w2',f.ops);await operationField(f.context,f.view,'machineType','HeadTable',f.ops);
 await operationCommand(f.context,f.view,'create-apply',f.ops);
 const pending=structuredClone(f.calls.find(call=>call.method==='CamOperation.Manage'));assert.ok(f.view.fiveOperationRequest);
 await operationField(f.context,f.view,'machineType','TableTable',f.ops);await operationCommand(f.context,f.view,'create-cancel',f.ops);
 assert.equal(f.view.fiveOperationCreateDialog.machineType,'HeadTable');f.domain.expected.mutationSequence='8';
 await operationCommand(f.context,f.view,'retry',f.ops);const edits=f.calls.filter(call=>call.method==='CamOperation.Manage');assert.equal(edits.length,2);assert.deepEqual(edits[1],pending);assert.equal(f.view.fiveOperationRequest,null);assert.equal(f.view.fiveOperationCreateDialog,null);
});
