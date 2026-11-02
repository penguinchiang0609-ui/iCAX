import test from 'node:test';
import assert from 'node:assert/strict';
import {getRibbonDefinition} from '../../apps/five-axis-cam/webpage/ribbon.mjs';
import {getProjectView,releaseProjectView} from '../../apps/_shared/workbench/state/projectViewStore.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {commandAvailability} from '../../apps/five-axis-cam/webpage/commandAvailability.mjs';

function fixture(t){
 const projectId=crypto.randomUUID(),view=getProjectView(projectId),machine={entityId:'machine',enabled:true,postprocessor:{name:'post'},elements:[{entityId:'machine',kind:'machine'},{entityId:'axis',parentEntityId:'machine',kind:'part'}],joints:[{entityId:'axis',type:'prismatic',axis:[1,0,0],position:0,lower:-100,upper:100}]};
 Object.assign(view,{fiveAxisArea:'simulation',fiveMachine:{machineId:machine.entityId,definitionId:'definition'},fiveMachineSelectedPartId:'axis',scene:{machines:[machine],machineDefinitions:[{id:'definition',enabled:true}],intentToolpaths:[],domain:{activeSetupId:'setup',placements:[],workpieces:[],setups:[{id:'setup',machineSetup:{machineId:machine.entityId}}],fixtureUnits:[]}}});
 selectTool(view,'machine-instances');
 t.after(()=>releaseProjectView(projectId));
 const ribbon=()=>getRibbonDefinition({project:{projectId}}),group=()=>ribbon().tabs.find(tab=>tab.id==='simulation').groups.find(group=>group.title==='机床实例'),main=()=>group().commands.find(command=>command.id==='five.tool.machine-instances');
 return {view,machine,ribbon,group,main};
}

test('instance ribbon keeps creation and one parameters entry while preserving existing context commands',t=>{
 const f=fixture(t),group=f.group(),main=f.main();
 assert.deepEqual(group.commands.map(command=>command.id),['five.machine-panel.instantiate','five.tool.machine-instances']);
 assert.equal(main.title,'实例参数');assert.equal(main.disabled,false);
 assert.deepEqual(main.menuItems.map(command=>command.id),['five.machine-panel.joints','five.machine-panel.colliders','five.machine-panel.post']);
 assert.ok(main.menuItems.every(command=>!command.disabled));
 for(const id of ['five.machine-ik.fk','five.machine-ik.translate'])assert.equal(commandAvailability(f.view,id).disabled,false,'existing floating command remains available');
 assert.equal(commandAvailability(f.view,'five.machine-ik.rotate').disabled,true,'a linear chain still cannot rotate');
});

test('definition management is one ribbon entry and instance creation opens a chooser independently of current selection',t=>{
 const f=fixture(t),definitions=f.ribbon().tabs.find(tab=>tab.id==='simulation').groups.find(group=>group.title==='机床定义');
 assert.deepEqual(definitions.commands.map(c=>[c.id,c.title]),[['five.tool.machine-definitions','管理']]);
 f.view.scene.machineDefinitions=[];
 assert.equal(commandAvailability(f.view,'five.machine-panel.instantiate').disabled,false);
 f.view.scene.machineDefinitions=[{id:'definition',enabled:false}];
 assert.equal(commandAvailability(f.view,'five.machine-panel.instantiate').disabled,false);
 f.view.fiveMachine.busy=true;
 assert.equal(commandAvailability(f.view,'five.machine-panel.instantiate').disabled,true);
});

test('the parameters page remains reachable without an instance while each instance-only menu action is unavailable',t=>{
 const f=fixture(t);f.view.scene.machines=[];
 assert.equal(f.main().disabled,false);
 for(const command of f.main().menuItems){assert.equal(command.disabled,true);assert.match(command.unavailableReason,/机床实例/);}
 f.view.scene.machines=[f.machine];f.view.fiveMachine.machineId='missing';
 assert.equal(f.main().disabled,false);assert.ok(f.main().menuItems.every(command=>command.disabled));
});

test('busy and pointer ownership disable main entry and children, then recover from current state',t=>{
 const f=fixture(t);
 const blocked=()=>{assert.equal(f.main().disabled,true);assert.ok(f.main().menuItems.every(command=>command.disabled));};
 f.view.pending=true;blocked();f.view.pending=false;
 f.view.fiveMachine.busy=true;blocked();f.view.fiveMachine.busy=false;
 f.view.viewport={transformManipulator:{pointerId:1}};blocked();f.view.viewport.transformManipulator.pointerId=null;
 assert.equal(f.main().disabled,false);assert.ok(f.main().menuItems.every(command=>!command.disabled));
});

test('job playback and output still require the current native report and post configuration',t=>{
 const f=fixture(t),command=id=>f.ribbon().tabs.find(tab=>tab.id==='simulation').groups.flatMap(group=>group.commands).find(command=>command.id==='five.machine-panel.'+id);
 assert.equal(command('play').disabled,true);assert.equal(command('export').disabled,true);
 f.view.fiveMachine.report={status:'Ready',collisionStatus:'Clear',playbackAllowed:true};
 assert.equal(command('play').disabled,false);assert.equal(command('export').disabled,false);
 f.view.fiveMachine.report={status:'Collision',collisionStatus:'Blocked',playbackAllowed:false};
 assert.equal(command('play').disabled,true);assert.equal(command('export').disabled,true);
 f.view.fiveMachine.report={status:'Ready',collisionStatus:'Clear',playbackAllowed:true};f.machine.postprocessor.name='';
 assert.equal(command('export').disabled,true);
});
