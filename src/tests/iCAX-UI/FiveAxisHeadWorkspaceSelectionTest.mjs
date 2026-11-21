import test from 'node:test';
import assert from 'node:assert/strict';
import {simulationCommand,simulationField} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';
import {renderHeadInstancePicker,headCalibrationInput,headInstanceField} from '../../apps/five-axis-cam/webpage/cuttingHeadInstances.mjs';
import {renderHeadWorkspaceParameters} from '../../apps/five-axis-cam/webpage/cuttingHeadWorkspace.mjs';
import {headWorkspaceSelection,headWorkspaceHead,hasHeadWorkspacePreview} from '../../apps/five-axis-cam/webpage/headWorkspaceSelection.mjs';
import {headScopeState,headScopeField,headScopeCommand,refreshHeadAssignments,canSetHeadScope,canInheritHeadScope} from '../../apps/five-axis-cam/webpage/headAssignmentScopes.mjs';

function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};}
async function until(predicate){for(let i=0;i<100;i++){if(predicate())return;await new Promise(setImmediate);}assert.fail('Head workspace operation did not settle');}
const browser=run=>async()=>{const previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};try{await run();}finally{globalThis.cancelAnimationFrame=previous;}};
const typesFor=id=>[id==='C'?'TableTable':'HeadHead'];
const definitions=['A','B','C','D'].map(id=>({id,name:'刀头 '+id,kind:id==='C'?'Fixed':'AB',machineTypes:typesFor(id)}));
function head(id,instanceId='instance-'+id){return {kind:'Builtin',builtinDefinitionId:id,name:'刀头 '+id,machineTypes:typesFor(id),tcpLocalPosition:[0,0,-200],beamLocalDirection:[0,0,-1],instance:{id:instanceId,revision:'1',name:'刀头 '+id,machineTypes:typesFor(id),parameters:{tcpMode:'Coordinates',tcp:[0,0,-200],beam:[0,0,-1],gaugeOrigin:[0,0,0],lengthMm:200,axes:[]}}};}

function fixture({workspace=true,existingCandidate=false,holdPreview=false,holdAssignments=false,loseFirstReceipt=false,rejectUse=false}={}){
 let current=true,revision=1,commits=0,lost=false;
 const first=head('A'),second=head(existingCandidate?'B':'C'),gates=new Map(),assignmentGates=[],receipts=new Map(),calls=[],events=[];
 const native={jobId:'job',expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'first',activePlacementId:'placement-first',setups:[{id:'first',name:'姿态 1',machineType:{machineType:'HeadHead'},headInstanceId:first.instance.id,headModel:first,rapidSettings:{}},{id:'second',name:'姿态 2',machineType:{machineType:existingCandidate?'HeadHead':'TableTable'},headInstanceId:second.instance.id,headModel:second,rapidSettings:{}}],headInstances:[first.instance,second.instance],workpieces:[{id:'workpiece'}],placements:[{id:'placement-first',setupId:'first',workpieceId:'workpiece',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],intentToolpaths:[],fixtures:[],fixtureUnits:[]};
 const view={pending:false,fiveAxisArea:'toolpath',fiveCamPage:workspace?'head':'cutting',fiveAxisUI:{tools:{toolpath:'select'}},fiveSimulation:{headOpen:true,builtinHeads:definitions,cursor:0,frames:[]},scene:{domain:structuredClone(native)}};
 const scopeSettings={Workpiece:{mode:'Inherit'},Project:{mode:'Inherit'},Product:{mode:'Inherit'}},setupModes=new Map();
 let preferencesRevision='preferences-7';
 // These receipt tests do not simulate the native inheritance algorithm.
 const assignments=setupId=>{const setup=native.setups.find(s=>s.id===setupId),model=setup?.headModel;return {scopes:{Setup:{mode:setupModes.get(setupId)||(model?.instance?'Instance':'None'),instanceId:model?.instance?.id||'',name:model?.name||''},...structuredClone(scopeSettings)},effective:{source:model?.instance?'Setup':'None',instanceId:model?.instance?.id||'',name:model?.name||''},productPreferencesRevision:preferencesRevision,productSnapshot:{mode:'Inherit'}};};
 headScopeState(view).data=assignments('first');
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[],ownerDocument:{querySelectorAll:()=>[]}},isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/tests/head-workspace.i5cam'},async autosave(){events.push({type:'save'});return {saved:true};}},sceneProxy:{async invoke(method,payload={}){
  calls.push({method,payload:structuredClone(payload)});
  if(method==='CamScene.ReadState')return structuredClone(native);
  if(method==='CamHead.Assignments'){
   const response=assignments(payload.setupId);
   if(holdAssignments){const gate=deferred();assignmentGates.push({setupId:payload.setupId,gate,response});return gate.promise;}
   return response;
  }
  if(method==='CamHead.Preview'){
   const model=payload.kind==='Instance'?native.setups.find(s=>s.headInstanceId===payload.instanceId)?.headModel:head(payload.builtinDefinitionId,'preview-'+payload.builtinDefinitionId);
   assert.ok(model,'The requested preview source exists');
   if(holdPreview){const gate=deferred();gates.set(model.builtinDefinitionId,gate);return gate.promise;}
   return {head:structuredClone(model)};
  }
  assert.ok(['CamHead.Assign','CamHead.Configure','CamHead.Instance'].includes(method),'Unexpected native method '+method);
  if(rejectUse)throw Error('CAM_HEAD_LOCKED');
  if(!receipts.has(payload.commandId)){
   assert.equal(payload.expected.mutationSequence,String(revision),'New mutations must use the latest scene revision');
   if(payload.scope==='Product')assert.equal(payload.preferencesRevision,preferencesRevision,'A new product preference edit must capture its queried revision');
   const target=native.setups.find(s=>s.id===payload.setupId);assert.ok(target,'Mutation names an existing setup');
   if(method==='CamHead.Assign'){
    const model=payload.kind==='Builtin'?head(payload.builtinDefinitionId):payload.kind==='Instance'?native.setups.find(s=>s.headInstanceId===payload.instanceId)?.headModel:null;
    if(payload.scope==='Setup'){
     setupModes.set(target.id,payload.action==='inherit'?'Inherit':payload.kind==='None'?'None':'Instance');
     if(payload.action==='set'){target.headModel=model?structuredClone(model):null;target.headInstanceId=model?.instance?.id||'';if(model&&!native.headInstances.some(i=>i.id===model.instance.id))native.headInstances.push(model.instance);}
    }else scopeSettings[payload.scope]=payload.action==='inherit'?{mode:'Inherit'}:payload.kind==='None'?{mode:'None'}:{mode:'Instance',instanceId:model?.instance?.id,name:model?.name};
    if(payload.scope==='Product')preferencesRevision='preferences-8';
   }else if(method==='CamHead.Configure'){
    const model=head(payload.builtinDefinitionId);target.headModel=model;target.headInstanceId=model.instance.id;
    if(!native.headInstances.some(i=>i.id===model.instance.id))native.headInstances.push(model.instance);
   }else{
    assert.equal(payload.action,'select');
    const source=native.setups.find(s=>s.headInstanceId===payload.instanceId);
    target.headModel=payload.instanceId?structuredClone(source.headModel):null;target.headInstanceId=payload.instanceId;
   }
   if(payload.scope!=='Product')native.expected.mutationSequence=String(++revision);commits++;receipts.set(payload.commandId,{changed:true});
  }
  if(loseFirstReceipt&&!lost){lost=true;throw Error('Transport acknowledgement lost');}
  return receipts.get(payload.commandId);
 }}};
 const ops={renderProject(){renderHeadInstancePicker(view,definitions,{list:view.fiveCamPage==='head'});events.push({type:'render',current,setupId:view.scene.domain.activeSetupId});},appendProjectLog(_context,level,message){events.push({type:'log',level,message});},async refreshSceneState(){assert.fail('Head use must not rebuild the rendered scene');}};
 ops.renderProject();
 return {view,context,ops,native,calls,events,gates,assignmentGates,assignments,commits:()=>commits,setCurrent:value=>{current=value;},browse:choice=>simulationField(context,view,'head-choice',choice,ops),use:()=>simulationCommand(context,view,'head-use',ops),activate(id){native.activeSetupId=id;view.scene.domain.activeSetupId=id;headScopeState(view);ops.renderProject();},leave(){view.fiveCamPage='cutting';ops.renderProject();},enter(){view.fiveCamPage='head';ops.renderProject();},mutations:()=>calls.filter(c=>['CamHead.Assign','CamHead.Configure','CamHead.Instance'].includes(c.method)),async browseReady(id){this.browse('builtin:'+id);await until(()=>view.fiveHeadSelection?.head?.builtinDefinitionId===id&&!view.fiveHeadSelection.loading);}};
}

test('workspace browsing is read-only and explicit use names only the current setup',browser(async()=>{
 const f=fixture(),before=structuredClone(f.native),other=structuredClone(f.native.setups[1]);
 await f.browseReady('B');
 assert.deepEqual(f.native,before);assert.equal(f.mutations().length,0);assert.equal(headWorkspaceHead(f.view).builtinDefinitionId,'B');assert.equal(f.view.scene.domain.setups[0].headModel.builtinDefinitionId,'A');
 const html=renderHeadWorkspaceParameters(f.view);assert.match(html,/当前使用：刀头 A/);assert.match(html,/正在预览/);assert.doesNotMatch(html,/data-five-head-instance-field="tcp\.0"/);
 await f.use();
 assert.equal(f.mutations().length,1);assert.equal(f.mutations()[0].payload.setupId,'first');assert.equal(f.native.setups[0].headModel.builtinDefinitionId,'B');assert.deepEqual(f.native.setups[1],other);assert.equal(hasHeadWorkspacePreview(f.view),false);assert.equal(headWorkspaceHead(f.view).builtinDefinitionId,'B');assert.equal(f.events.filter(e=>e.type==='save').length,1);
}));

test('only the latest catalog preview can publish its asynchronous response',browser(async()=>{
 const f=fixture({holdPreview:true});f.browse('builtin:B');await until(()=>f.gates.has('B'));f.browse('builtin:D');await until(()=>f.gates.has('D'));
 f.gates.get('D').resolve({head:head('D','preview-D')});await until(()=>headWorkspaceHead(f.view)?.builtinDefinitionId==='D');
 f.gates.get('B').resolve({head:head('B','preview-B')});await new Promise(setImmediate);
 assert.equal(headWorkspaceHead(f.view).builtinDefinitionId,'D');assert.equal(headWorkspaceSelection(f.view).choice,'builtin:D');assert.equal(headWorkspaceSelection(f.view).loading,false);assert.equal(f.mutations().length,0);
}));

test('a late preview cannot follow the user into another setup or project',browser(async()=>{
 for(const change of ['setup','project']){
  const f=fixture({holdPreview:true});f.browse('builtin:B');await until(()=>f.gates.has('B'));
  if(change==='setup')f.activate('second');else f.setCurrent(false);
  const renders=f.events.filter(e=>e.type==='render').length;
  f.gates.get('B').resolve({head:head('B','preview-B')});await new Promise(setImmediate);
  assert.equal(f.events.filter(e=>e.type==='render').length,renders,'An old reply cannot repaint the newer context');assert.equal(f.mutations().length,0);
  if(change==='setup'){assert.equal(hasHeadWorkspacePreview(f.view),false);assert.equal(headWorkspaceHead(f.view).builtinDefinitionId,'C');}
 }
}));

test('leaving the workspace during a preview cannot leave it loading forever on return',browser(async()=>{
 const f=fixture({holdPreview:true});f.browse('builtin:B');await until(()=>f.gates.has('B'));f.leave();f.gates.get('B').resolve({head:head('B','preview-B')});await new Promise(setImmediate);f.enter();
 assert.equal(!!headWorkspaceSelection(f.view).loading,false);assert.ok(headWorkspaceHead(f.view),'Returning shows either the current head or the completed candidate');assert.equal(f.mutations().length,0);
}));

test('an existing instance cannot be applied to a setup entered during an asynchronous flush',browser(async()=>{
 const f=fixture({existingCandidate:true}),other=structuredClone(f.native.setups[1]);await f.browseReady('B');
 const applying=f.use();await Promise.resolve();f.activate('second');await applying;
 assert.ok(f.mutations().every(c=>c.payload.setupId==='first'),'Every accepted request must retain the setup captured by the Use gesture');assert.deepEqual(f.native.setups[1],other);assert.equal(f.events.some(e=>e.type==='log'&&e.level==='ok'),false,'The newly active setup must not receive an old success message');
}));

for(const existingCandidate of [false,true])test(`an unknown ${existingCandidate?'existing instance':'builtin'} use receipt retries the original request without claiming success early`,browser(async()=>{
 const f=fixture({loseFirstReceipt:true,existingCandidate});await f.browseReady('B');await f.use();
 assert.equal(f.commits(),1);assert.equal(f.mutations().length,1);assert.equal(hasHeadWorkspacePreview(f.view),true);assert.equal(f.view.scene.domain.setups[0].headModel.builtinDefinitionId,'A');assert.equal(f.events.filter(e=>e.type==='save').length,0);assert.equal(f.events.some(e=>e.type==='log'&&e.level==='ok'),false);
 const original=structuredClone(f.mutations()[0]);await f.use();assert.equal(f.commits(),1);assert.equal(f.mutations().length,2);assert.deepEqual(f.mutations()[1],original);assert.equal(hasHeadWorkspacePreview(f.view),false);assert.equal(headWorkspaceHead(f.view).builtinDefinitionId,'B');assert.equal(f.events.filter(e=>e.type==='save').length,1);
}));

test('a failed preview cannot enable assignment or replace the actual current head',browser(async()=>{
 const f=fixture({holdPreview:true});f.browse('builtin:B');await until(()=>f.gates.has('B'));f.gates.get('B').reject(Error('Preview resource unavailable'));await until(()=>!headWorkspaceSelection(f.view).loading);await f.use();
 assert.equal(f.mutations().length,0);assert.equal(f.native.setups[0].headModel.builtinDefinitionId,'A');assert.match(headWorkspaceSelection(f.view).error,/Preview resource unavailable/);const html=renderHeadWorkspaceParameters(f.view);assert.match(html,/当前使用：刀头 A/);assert.match(html,/Preview resource unavailable/);
}));

test('a rejected assignment keeps the preview and actual current head separate',browser(async()=>{
 const f=fixture({rejectUse:true});await f.browseReady('B');await f.use();assert.equal(f.commits(),0);assert.equal(hasHeadWorkspacePreview(f.view),true);assert.equal(f.native.setups[0].headModel.builtinDefinitionId,'A');assert.match(f.view.fiveHeadAssignments.error,/CAM_HEAD_LOCKED/);assert.equal(f.events.some(e=>e.type==='log'&&e.level==='ok'),false);
 const html=renderHeadWorkspaceParameters(f.view);assert.match(html,/当前使用：刀头 A/);assert.match(html,/CAM_HEAD_LOCKED/);
}));

test('Unassigned remains a local preview until explicit use detaches only the current setup',browser(async()=>{
 const f=fixture(),other=structuredClone(f.native.setups[1]);f.browse('');await until(()=>headWorkspaceSelection(f.view).choice==='');assert.equal(f.mutations().length,0);assert.equal(headWorkspaceHead(f.view),null);assert.equal(f.native.setups[0].headInstanceId,'instance-A');await f.use();assert.equal(f.mutations()[0].payload.setupId,'first');assert.equal(f.mutations()[0].payload.kind,'None');assert.equal(f.native.setups[0].headInstanceId,'');assert.deepEqual(f.native.setups[1],other);assert.equal(hasHeadWorkspacePreview(f.view),false);
}));

test('candidate preview parameters cannot edit the actual project head',browser(async()=>{
 const f=fixture();await f.browseReady('B');const draft=structuredClone(f.view.fiveHeadEditor.draft);headInstanceField(f.context,f.view,'tcp.0','123',f.ops);assert.deepEqual(f.view.fiveHeadEditor.draft,draft);assert.throws(()=>headCalibrationInput(f.view),/先将所选切割头用于当前工序/);assert.equal(f.mutations().length,0);
}));

test('the existing head dropdown outside the workspace still applies and saves immediately',browser(async()=>{
 const f=fixture({workspace:false});f.browse('builtin:B');await until(()=>!f.view.pending&&!f.view.fiveSimulation.headChoiceBusy&&f.events.some(e=>e.type==='save'));assert.equal(f.mutations().length,1);assert.equal(f.mutations()[0].payload.setupId,'first');assert.equal(f.native.setups[0].headModel.builtinDefinitionId,'B');assert.equal(f.calls.some(c=>c.method==='CamHead.Preview'),false);
}));

for(const scope of ['Setup','Workpiece','Project','Product'])test(`the ${scope} gesture sends one captured scope and selected head to the native assignment command`,browser(async()=>{
 const f=fixture();await f.browseReady('B');headScopeField(f.context,f.view,scope,f.ops);await f.use();
 assert.equal(f.mutations().length,1);const request=f.mutations()[0];assert.equal(request.method,'CamHead.Assign');assert.equal(request.payload.setupId,'first');assert.equal(request.payload.scope,scope);assert.equal(request.payload.action,'set');assert.equal(request.payload.kind,'Builtin');assert.equal(request.payload.builtinDefinitionId,'B');assert.equal(request.payload.expected.mutationSequence,'1');
 if(scope==='Product')assert.equal(request.payload.preferencesRevision,'preferences-7');else assert.equal(Object.hasOwn(request.payload,'preferencesRevision'),false);
}));

test('setting the product default does not autosave or refresh the current project scene',browser(async()=>{
 const f=fixture(),nativeBefore=structuredClone(f.native),viewBefore=structuredClone(f.view.scene.domain);
 await f.browseReady('B');headScopeField(f.context,f.view,'Product',f.ops);const beforeUseCalls=f.calls.length;await f.use();
 assert.deepEqual(f.native,nativeBefore);assert.deepEqual(f.view.scene.domain,viewBefore);assert.equal(f.events.filter(e=>e.type==='save').length,0);assert.equal(f.calls.slice(beforeUseCalls).some(c=>c.method==='CamScene.ReadState'),false);assert.equal(f.calls.slice(beforeUseCalls).filter(c=>c.method==='CamHead.Assignments').length,1);
 assert.equal(headScopeState(f.view).data.productPreferencesRevision,'preferences-8');assert.equal(headScopeState(f.view).data.scopes.Product.name,'刀头 B');assert.equal(headWorkspaceHead(f.view).builtinDefinitionId,'B','The selected prospective default can remain a preview while the current pose keeps its head');
 const html=renderHeadWorkspaceParameters(f.view);assert.match(html,/当前使用：刀头 A/);assert.match(html,/仅用于以后新建工程；只供匹配机型使用，已有工程保持原配置/);
}));

test('explicit None and restoring inheritance submit different native actions',browser(async()=>{
 const f=fixture();f.browse('');await until(()=>headWorkspaceSelection(f.view).choice==='');await f.use();
 assert.equal(f.mutations()[0].payload.kind,'None');assert.equal(f.mutations()[0].payload.action,'set');assert.equal(canInheritHeadScope(f.view),true);
 await headScopeCommand(f.context,f.view,'inherit',f.ops);const restore=f.mutations()[1];assert.equal(restore.payload.action,'inherit');assert.equal(restore.payload.scope,'Setup');assert.equal(restore.payload.setupId,'first');assert.equal(Object.hasOwn(restore.payload,'kind'),false);assert.notEqual(restore.payload.commandId,f.mutations()[0].payload.commandId);assert.equal(canInheritHeadScope(f.view),false);
}));

test('an unacknowledged product default preserves the original scope selector and preferences revision on retry',browser(async()=>{
 const f=fixture({loseFirstReceipt:true});await f.browseReady('B');headScopeField(f.context,f.view,'Product',f.ops);await f.use();
 const original=structuredClone(f.mutations()[0]);assert.equal(f.commits(),1);assert.equal(original.payload.preferencesRevision,'preferences-7');assert.ok(headScopeState(f.view).request);assert.equal(f.events.some(e=>e.type==='log'&&e.level==='ok'),false);
 headScopeField(f.context,f.view,'Setup',f.ops);assert.equal(headScopeState(f.view).scope,'Product','A pending receipt cannot change target scope');
 await f.use();assert.equal(f.commits(),1);assert.equal(f.mutations().length,2);assert.deepEqual(f.mutations()[1],original);assert.equal(headScopeState(f.view).request,null);assert.equal(f.events.filter(e=>e.type==='save').length,0);assert.equal(f.view.scene.domain.setups[0].headModel.builtinDefinitionId,'A');
}));

test('only the latest assignment query publishes its returned scope settings',browser(async()=>{
 const f=fixture({holdAssignments:true});const first=refreshHeadAssignments(f.context,f.view,f.ops),second=refreshHeadAssignments(f.context,f.view,f.ops);await until(()=>f.assignmentGates.length===2);
 const recent={...f.assignmentGates[1].response,productPreferencesRevision:'recent-preferences'};f.assignmentGates[1].gate.resolve(recent);await second;
 f.assignmentGates[0].gate.resolve({...f.assignmentGates[0].response,productPreferencesRevision:'obsolete-preferences'});await first;
 assert.equal(headScopeState(f.view).data.productPreferencesRevision,'recent-preferences');assert.equal(headScopeState(f.view).loading,false);assert.equal(f.mutations().length,0);
}));

test('a scope query reply for a previous setup cannot overwrite the new setup panel',browser(async()=>{
 const f=fixture({holdAssignments:true}),query=refreshHeadAssignments(f.context,f.view,f.ops);await until(()=>f.assignmentGates.length===1);f.activate('second');const current=headScopeState(f.view),renders=f.events.filter(e=>e.type==='render').length;
 f.assignmentGates[0].gate.resolve(f.assignmentGates[0].response);await query;
 assert.equal(headScopeState(f.view),current);assert.equal(current.setupId,'second');assert.equal(current.data,undefined);assert.equal(f.events.filter(e=>e.type==='render').length,renders);assert.equal(f.mutations().length,0);
}));

test('a scope query discarded after switching projects can be refreshed when that project becomes active again',browser(async()=>{
 const f=fixture({holdAssignments:true}),query=refreshHeadAssignments(f.context,f.view,f.ops);await until(()=>f.assignmentGates.length===1);const before=structuredClone(headScopeState(f.view).data);f.setCurrent(false);const renders=f.events.filter(e=>e.type==='render').length;
 f.assignmentGates[0].gate.resolve({...f.assignmentGates[0].response,productPreferencesRevision:'obsolete-hidden-project'});await query;
 assert.deepEqual(headScopeState(f.view).data,before);assert.equal(f.events.filter(e=>e.type==='render').length,renders);f.setCurrent(true);assert.equal(headScopeState(f.view).loading,false,'An abandoned query must not leave the reactivated project permanently loading');
 const retried=refreshHeadAssignments(f.context,f.view,f.ops);await until(()=>f.assignmentGates.length===2);f.assignmentGates[1].gate.resolve(f.assignmentGates[1].response);await retried;assert.equal(headScopeState(f.view).loading,false);assert.equal(f.mutations().length,0);
}));

test('an inherited effective head can be explicitly pinned to the pose without choosing a different head',browser(async()=>{
 const f=fixture(),state=headScopeState(f.view);state.data.scopes.Setup={mode:'Inherit'};state.data.effective.source='Workpiece';assert.equal(hasHeadWorkspacePreview(f.view),false);assert.equal(canSetHeadScope(f.view),true);assert.equal(canInheritHeadScope(f.view),false);
 await f.use();const request=f.mutations()[0];assert.equal(request.payload.scope,'Setup');assert.equal(request.payload.action,'set');assert.equal(request.payload.kind,'Instance');assert.equal(request.payload.instanceId,'instance-A');assert.equal(canSetHeadScope(f.view),false);
}));

test('system and personal lists admit only native machineTypes metadata and never infer compatibility from labels',browser(async()=>{
 const f=fixture();f.view.fiveHeadEditor.entries=[{id:'right',name:'单摆头但实际是双摆头',machineTypes:['HeadHead']},{id:'wrong',name:'双摆头但实际固定',machineTypes:['TableTable']},{id:'unknown',name:'AB 双摆头',machineTypes:[]}];
 const html=renderHeadInstancePicker(f.view,[...definitions,{id:'unknown',name:'双摆头 AB',kind:'AB',machineTypes:[]}],{list:true});
 assert.match(html,/data-head-choice-value="builtin:B"/);assert.match(html,/data-head-choice-value="profile:right"/);
 assert.doesNotMatch(html,/data-head-choice-value="builtin:C"|data-head-choice-value="profile:wrong"|data-head-choice-value="profile:unknown"|data-head-choice-value="builtin:unknown"/);
 f.browse('builtin:C');await until(()=>!headWorkspaceSelection(f.view).loading);assert.equal(f.calls.some(c=>c.method==='CamHead.Preview'),false);assert.equal(canSetHeadScope(f.view),false);assert.match(headWorkspaceSelection(f.view).error,/不适用于本工序机型/);
}));

test('the authoritative preview metadata is checked again before a candidate can be used',browser(async()=>{
 const f=fixture({holdPreview:true});f.browse('builtin:B');await until(()=>f.gates.has('B'));
 f.gates.get('B').resolve({head:{...head('B','preview-B'),machineTypes:['TableTable']}});await until(()=>!headWorkspaceSelection(f.view).loading);
 assert.equal(canSetHeadScope(f.view),false);await f.use();assert.equal(f.mutations().length,0);assert.match(headWorkspaceSelection(f.view).error,/不适用于本工序机型/);
}));
