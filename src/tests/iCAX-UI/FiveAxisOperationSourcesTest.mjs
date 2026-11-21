import test from 'node:test';
import assert from 'node:assert/strict';
import {operationPlacements,prepareOperationArea,operationCommand,operationField,renderOperationParameters,syncOperationPreparationKind} from '../../apps/five-axis-cam/webpage/operationWorkspace.mjs';
import {renderPreparationTree} from '../../apps/five-axis-cam/webpage/preparationTree.mjs';
import {workpieceSelectionBusy,selectWorkpiece} from '../../apps/five-axis-cam/webpage/workpieceSelection.mjs';
import {projectWorkpiece} from '../../apps/five-axis-cam/webpage/domainActions.mjs';
import {fixtureCommand} from '../../apps/five-axis-cam/webpage/fixtureActions.mjs';
import {camRibbonWorkspace,camContextSelectors} from '../../apps/five-axis-cam/webpage/camWorkspace.mjs';
import {canCreateOperation} from '../../apps/five-axis-cam/webpage/operationCreateDialog.mjs';
import {commandAvailability} from '../../apps/five-axis-cam/webpage/commandAvailability.mjs';

function fixture(){
 const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'',workpieces:[{id:'first',name:'第一工件',enabled:false},{id:'second',name:'第二工件',enabled:true}],setups:[{id:'a',name:'禁用工件的工序',enabled:true},{id:'b',name:'禁用工序',enabled:false},{id:'c',name:'可加工工序',enabled:true}],placements:[{id:'source-first',setupId:'',workpieceId:'first'},{id:'source-second',setupId:'',workpieceId:'second'},{id:'placement-a',setupId:'a',workpieceId:'first'},{id:'placement-b',setupId:'b',workpieceId:'second'},{id:'placement-c',setupId:'c',workpieceId:'second'}],fixtureUnits:[],fixtures:[],intentToolpaths:[]};
 return {fiveAxisArea:'workpiece',fivePreparationKind:'workpiece',fiveAxisUI:{tools:{workpiece:'select',operation:'machine-type'}},scene:{domain,activePlacementId:'source-first'}};
}
test('operation management retains disabled references but machining candidates exclude disabled workpieces and operations',()=>{
 const view=fixture();
 assert.deepEqual(operationPlacements(view).map(p=>p.id),['placement-a','placement-b','placement-c']);
 assert.deepEqual(operationPlacements(view,{enabledOnly:true}).map(p=>p.id),['placement-c']);
});
test('entering toolpath or twin with no eligible operations performs no activation and does not reject the page',async()=>{
 const view=fixture();view.scene.domain.setups.find(s=>s.id==='c').enabled=false;view.fiveOperationPlacementId='placement-a';
 const context={isCurrentProject:()=>true,sceneProxy:{invoke(){assert.fail('No disabled placement may be activated');}}};
 const ops={renderProject(){assert.fail('The preparation function must not render/retry an ineligible candidate');}};
 assert.equal(await prepareOperationArea(context,view,ops,'toolpath'),true);
 assert.equal(await prepareOperationArea(context,view,ops,'simulation'),true);
 assert.equal(view.scene.activePlacementId,'source-first');
});
test('the shared workpiece tree contains no operation or fixture child rows',()=>{
 const html=renderPreparationTree(fixture());
 assert.match(html,/aria-label="工件列表"/);
 assert.match(html,/第一工件/);assert.match(html,/第二工件/);
 assert.doesNotMatch(html,/data-five-setup-drag|five\.setup\.activate|five\.preparation\.unit/);
});
for(const area of ['operation','workpiece'])test(`all-disabled toolpath can activate the ${area} target before that area is mounted`,async()=>{
 const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{
  const view=fixture(),domain=view.scene.domain;
  for(const workpiece of domain.workpieces){workpiece.enabled=false;workpiece.geometryRef={url:'resource://'+workpiece.id};}
  view.fiveAxisArea='toolpath';view.scene.activePlacementId='';view.fiveOperationPlacementId='placement-b';
  domain.activePlacementId=area==='operation'?'source-first':'placement-a';domain.activeSetupId=area==='operation'?'':'a';
  const activated=[],targetId=area==='operation'?'placement-a':'source-first';
  const context={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){
   assert.equal(method,'CamWorkpiece.Activate');activated.push(payload.placementId);const target=domain.placements.find(p=>p.id===payload.placementId);domain.activePlacementId=target.id;domain.activeSetupId=target.setupId||'';return {};
  }}};
  const ops={renderProject(){},async refreshSceneState(){projectWorkpiece(view.scene,view.fiveActivePlacementId,true);return true;},fitViewAfterRenderPublish(){assert.fail('Cross-area activation must not fit the old filtered viewport');}};
  assert.equal(await prepareOperationArea(context,view,ops,area),true);
  assert.deepEqual(activated,[targetId]);assert.equal(domain.activePlacementId,targetId);
  assert.equal(view.fiveAxisArea,'toolpath','Preparation must not falsify the current area');
  assert.equal(view.scene.activePlacementId,'','The old machining projection still excludes disabled workpieces');
  assert.equal(view.scene.model.isLoaded,false);
  view.fiveAxisArea=area;projectWorkpiece(view.scene,view.fiveActivePlacementId,false);
  assert.equal(view.scene.activePlacementId,targetId);assert.equal(view.scene.model.isLoaded,true);
 }finally{globalThis.cancelAnimationFrame=previousCancel;}
});
test('ordinary toolpath selection still refuses a disabled workpiece without the explicit destination area',async()=>{
 const view=fixture();view.fiveAxisArea='toolpath';
 const context={isCurrentProject:()=>true,sceneProxy:{invoke(){assert.fail('The disabled workpiece is not a toolpath candidate');}}};
 await selectWorkpiece(context,view,'placement-a',{});
 assert.equal(view.scene.activePlacementId,'source-first');
});
test('opening support design switches to operation before owning the fixture request',async()=>{
 const view=fixture();view.fiveAxisArea='toolpath';view.activeAreaId='toolpath';view.scene.domain.activeSetupId='c';view.scene.activePlacementId='placement-c';
 const navigation=[],context={isCurrentProject:()=>true,actions:{async selectRibbonTab(area){assert.equal(Boolean(view.fixtureRequest),false);navigation.push(area);view.fiveAxisArea=area;view.activeAreaId=area;return area;}}},ops={renderProject(){}};
 await fixtureCommand(context,view,'generate',ops);
 assert.deepEqual(navigation,['operation']);assert.equal(view.fiveAxisArea,'operation');assert.equal(view.fiveFixtureDesignOpen,true);assert.equal(view.fixtureRequest,null);
});
for(const action of ['generate','edit:unit','focus.design','select:unit'])test(`a rejected operation navigation preserves the prior fixture editor for ${action}`,async()=>{
 const view=fixture();view.fiveAxisArea='toolpath';view.activeAreaId='toolpath';view.fiveFixtureDesignOpen=false;view.fixtureDesignDraft={unchanged:true};
 const before=structuredClone(view),navigation=[];
 const context={isCurrentProject:()=>true,actions:{async selectRibbonTab(area){assert.equal(Boolean(view.fixtureRequest),false);navigation.push(area);return 'toolpath';}},sceneProxy:{invoke(){assert.fail('Rejected navigation must not read or change the fixture');}}};
 await fixtureCommand(context,view,action,{renderProject(){assert.fail('The existing editor must remain intact');}});
 assert.deepEqual(navigation,['operation']);assert.deepEqual(view,before);
});
test('an isolated fixture editor without an app area can retain its local unit-test context',async()=>{
 const view=fixture();delete view.fiveAxisArea;
 await fixtureCommand({isCurrentProject:()=>true},view,'generate',{renderProject(){}});
 assert.equal(view.fiveFixtureDesignOpen,true);
});
test('toolpath and twin selectors disable operations that reference disabled workpieces while management retains them',()=>{
 const view=fixture();view.scene.domain.activeSetupId='c';view.scene.activePlacementId='placement-c';
 for(const area of ['toolpath','simulation','operation']){
  view.fiveAxisArea=area;
  const workspace=camRibbonWorkspace(view,area);
  const selectors=camContextSelectors(view,area);if(area!=='simulation')assert.deepEqual(workspace.selectors,[]);
  if(area==='operation'){assert.deepEqual(selectors.map(s=>s.id),['five-workpiece']);assert.equal(selectors[0].disabled,false);assert.deepEqual(selectors[0].options.map(o=>o.value),['first','second']);assert.equal(workspace.collapsibleSelectors,false);assert.equal(operationPlacements(view).length,3);continue;}
  const options=selectors.find(s=>s.id==='five-pose').options;
  assert.equal(options.find(o=>o.value==='placement-a').disabled,true);
  assert.equal(options.find(o=>o.value==='placement-b').disabled,true);
  assert.equal(options.find(o=>o.value==='placement-c').disabled,false);
 }
});
for(const failure of [false,true])test(`operation publication remains busy through delayed viewport fitting and releases on ${failure?'failure':'completion'}`,async()=>{
 const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{
  const view=fixture();view.fiveAxisArea='operation';view.scene.domain.workpieces[0].enabled=true;
  let resolveFit,rejectFit,fitEntered=false,creates=0,activations=0;
  const fit=new Promise((resolve,reject)=>{resolveFit=resolve;rejectFit=reject;});
  const context={isCurrentProject:()=>true,sceneProxy:{async invoke(method){if(method==='CamOperation.Manage'){creates++;return {setupId:'new-operation',placementId:'new-placement',workpieceId:'first'};}if(method==='CamWorkpiece.Activate'){activations++;return {};}throw Error('Unexpected '+method);}}};
  const ops={renderProject(){},appendProjectLog(){},async refreshSceneState(){
   view.scene.domain.setups.push({id:'new-operation',name:'新工序'});view.scene.domain.placements.push({id:'new-placement',setupId:'new-operation',workpieceId:'first'});view.scene.domain.activeSetupId='new-operation';view.scene.activePlacementId='new-placement';view.pending=false;return true;
  },async fitViewAfterRenderPublish(){fitEntered=true;return fit;}};
  await operationCommand(context,view,'create',ops);
  await operationField(context,view,'workpieceId','first',ops);
  await operationField(context,view,'machineType','HeadHead',ops);
  const publication=operationCommand(context,view,'create-apply',ops);
  for(let i=0;i<50&&!fitEntered;i++)await new Promise(setImmediate);
  assert.equal(fitEntered,true);assert.equal(view.pending,false,'Shared scene refresh may release its own pending flag');
  assert.equal(workpieceSelectionBusy(view),true);
  await operationCommand(context,view,'create',ops);
  await operationCommand(context,view,'create-apply',ops);
  await selectWorkpiece(context,view,'placement-c',ops);
  assert.equal(creates,1);assert.equal(activations,0);assert.equal(view.scene.activePlacementId,'new-placement');
  if(failure)rejectFit(Error('Fit failed'));else resolveFit({fitted:true});
  await publication;
  assert.equal(view.fiveOperationBusy,false);assert.equal(view.pending,false);assert.equal(workpieceSelectionBusy(view),false);
  if(failure)assert.match(view.error,/Fit failed/);
 }finally{globalThis.cancelAnimationFrame=previousCancel;}
});
test('new operation binds the selected workpiece and confirms exactly one creation after choosing its machine',async()=>{
 const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{
  const view=fixture();view.fiveAxisArea='operation';view.scene.domain.workpieces[0].enabled=true;view.scene.domain.activeSetupId='c';view.scene.activePlacementId='placement-c';
  const before=structuredClone(view.scene.domain),calls=[];
  const context={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){calls.push({method,payload:structuredClone(payload)});assert.equal(method,'CamOperation.Manage');return {setupId:'created',placementId:'created-placement',workpieceId:payload.workpieceId};}}};
  const ops={renderProject(){},appendProjectLog(){},async refreshSceneState(){view.scene.domain.setups.push({id:'created'});view.scene.domain.placements.push({id:'created-placement',setupId:'created',workpieceId:'second'});view.scene.domain.activeSetupId='created';view.scene.activePlacementId='created-placement';return true;},async fitViewAfterRenderPublish(){return {fitted:true};}};
  await operationCommand(context,view,'create',ops);
  assert.deepEqual(view.fiveOperationCreateDialog,{workpieceId:'second',workpieceName:'第二工件',epoch:'epoch',machineType:''});
  assert.equal(canCreateOperation(view),false);
  await operationCommand(context,view,'create-apply',ops);
  assert.equal(calls.length,0);
  await operationField(context,view,'workpieceId','first',ops);
  assert.equal(view.fiveOperationCreateDialog.workpieceId,'second');assert.deepEqual(view.scene.domain,before);assert.equal(calls.length,0);
  assert.equal(canCreateOperation(view),false);
  await operationField(context,view,'machineType','Bogus',ops);
  await operationCommand(context,view,'create-apply',ops);assert.equal(calls.length,0);
  await operationField(context,view,'machineType','TableTable',ops);
  assert.equal(canCreateOperation(view),true);
  const html=renderOperationParameters(view);assert.doesNotMatch(html,/five.operation.create-apply|operation-field-machineType/);
  assert.equal(view.scene.activePlacementId,'placement-c','The modal leaves the current viewport source intact');
  const pending=operationCommand(context,view,'create-apply',ops);
  await operationCommand(context,view,'create-apply',ops);
  await pending;
  await operationCommand(context,view,'create-apply',ops);
  assert.equal(calls.length,1);assert.equal(calls[0].payload.action,'create');assert.equal(calls[0].payload.workpieceId,'second');assert.equal(calls[0].payload.machineType,'TableTable');assert.equal(view.fiveOperationCreateDialog,null);
 }finally{globalThis.cancelAnimationFrame=previousCancel;}
});
test('canceling new operation discards its machine draft without mutating the existing operation',async()=>{
 const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{
  const view=fixture();view.fiveAxisArea='operation';view.scene.domain.workpieces[0].enabled=true;view.scene.domain.activeSetupId='c';view.scene.activePlacementId='placement-c';
  const before=structuredClone(view.scene.domain),context={isCurrentProject:()=>true,sceneProxy:{invoke(){assert.fail('Draft and cancellation must not invoke the scene');}}},ops={renderProject(){}};
  await operationCommand(context,view,'create',ops);await operationField(context,view,'machineType','HeadTable',ops);await operationCommand(context,view,'create-cancel',ops);
  assert.equal(view.fiveOperationCreateDialog,null);assert.deepEqual(view.scene.domain,before);assert.equal(view.scene.activePlacementId,'placement-c');
 }finally{globalThis.cancelAnimationFrame=previousCancel;}
});
test('a background read does not block local modal choices or cancel while creation itself remains guarded',async()=>{
 const view=fixture();view.fiveAxisArea='operation';view.scene.activePlacementId='source-second';
 const context={isCurrentProject:()=>true,sceneProxy:{invoke(){assert.fail('A read in progress cannot publish a new operation');}}},ops={renderProject(){}};
 await operationCommand(context,view,'create',ops);view.pending=true;
 await operationField(context,view,'workpieceId','second',ops);await operationField(context,view,'machineType','HeadTable',ops);
 assert.deepEqual(view.fiveOperationCreateDialog,{workpieceId:'second',workpieceName:'第二工件',epoch:'epoch',machineType:'HeadTable'});assert.equal(canCreateOperation(view),false);
 await operationCommand(context,view,'create-apply',ops);
 await operationCommand(context,view,'create-cancel',ops);assert.equal(view.fiveOperationCreateDialog,null);
});
for(const change of ['workpiece','epoch'])test(`a creation draft refuses a changed ${change} context`,async()=>{
 const view=fixture();view.fiveAxisArea='operation';view.scene.activePlacementId='source-second';view.scene.domain.workpieces[0].enabled=true;
 const context={isCurrentProject:()=>true,sceneProxy:{invoke(){assert.fail('A stale draft cannot create an operation');}}},ops={renderProject(){}};
 await operationCommand(context,view,'create',ops);await operationField(context,view,'machineType','HeadTable',ops);assert.equal(canCreateOperation(view),true);
 if(change==='workpiece')view.scene.activePlacementId='source-first';else view.scene.domain.expected.epoch='new-epoch';
 assert.equal(canCreateOperation(view),false);await operationCommand(context,view,'create-apply',ops);assert.match(view.error,/当前工件已变化/);
});
test('a disabled current workpiece never borrows an enabled workpiece for creation',async()=>{
 const view=fixture(),context={isCurrentProject:()=>true},ops={renderProject(){}};
 await operationCommand(context,view,'create',ops);assert.equal(view.fiveOperationCreateDialog,undefined);assert.match(view.error,/当前工件已禁用/);
});
for(const area of ['operation','toolpath','simulation'])test(`a workpiece with no operations keeps its source on ${area}`,async()=>{
 const view=fixture(),d=view.scene.domain;d.workpieces[0].enabled=true;d.setups=d.setups.filter(s=>s.id!=='a');d.placements=d.placements.filter(p=>p.id!=='placement-a');view.fiveOperationPlacementId='placement-c';view.fiveCamPlacements=new Map([['second','placement-c']]);
 const context={isCurrentProject:()=>true,sceneProxy:{invoke(){assert.fail('Other workpiece operations cannot steal the source context');}}};
 assert.equal(await prepareOperationArea(context,view,{},area),true);assert.equal(view.scene.activePlacementId,'source-first');
});
test('returning to a workpiece restores its own remembered operation',async()=>{
 const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{
  const view=fixture(),d=view.scene.domain;view.fiveAxisArea='operation';view.scene.activePlacementId='source-second';d.activePlacementId='source-second';view.fiveOperationPlacementId='placement-a';view.fiveCamPlacements=new Map([['first','placement-a'],['second','placement-c']]);
  const calls=[],context={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){calls.push(payload.placementId);d.activePlacementId=payload.placementId;return {};}}},ops={renderProject(){},async refreshSceneState(){view.scene.activePlacementId=d.activePlacementId;return true;},async fitViewAfterRenderPublish(){}};
  assert.equal(await prepareOperationArea(context,view,ops,'operation'),true);assert.deepEqual(calls,['placement-c']);assert.equal(view.fiveCamPlacements.get('second'),'placement-c');
 }finally{globalThis.cancelAnimationFrame=previousCancel;}
});
test('returning from workpiece to the restored operation enables support after the final selection refresh',async()=>{
 const previousCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{
  const view=fixture(),d=view.scene.domain;view.scene.activePlacementId='source-second';d.activePlacementId='source-second';view.fiveCamPlacements=new Map([['second','placement-c']]);
  view.viewport={sceneObjects:new Map([['placement-c',{geometry:{}}]])};
  const context={isCurrentProject:()=>true,sceneProxy:{async invoke(_method,payload){d.activePlacementId=payload.placementId;d.activeSetupId=d.placements.find(p=>p.id===payload.placementId).setupId;return {};}}};
  const ops={renderProject(){syncOperationPreparationKind(view);},async refreshSceneState(){view.scene.activePlacementId=d.activePlacementId;return true;}};
  assert.equal(await prepareOperationArea(context,view,ops,'operation'),true);assert.equal(view.fivePreparationKind,'workpiece','The old workpiece page still owns preparation kind during activation');
  view.fiveAxisArea='operation';syncOperationPreparationKind(view);
  assert.equal(view.scene.activePlacementId,'placement-c');assert.equal(view.fivePreparationKind,'setup');assert.equal(commandAvailability(view,'five.fixture.generate').disabled,false);
  view.fivePreparationKind='workpiece';syncOperationPreparationKind(view);assert.equal(view.fivePreparationKind,'setup','A final render repairs the kind after a placement binding reset');assert.equal(commandAvailability(view,'five.fixture.generate').disabled,false);
  view.fivePreparationKind='unit';syncOperationPreparationKind(view);assert.equal(view.fivePreparationKind,'unit','A valid fixture selection is preserved');
  view.scene.activePlacementId='source-second';syncOperationPreparationKind(view);assert.equal(view.fivePreparationKind,'workpiece');assert.equal(commandAvailability(view,'five.fixture.generate').disabled,true);
 }finally{globalThis.cancelAnimationFrame=previousCancel;}
});
