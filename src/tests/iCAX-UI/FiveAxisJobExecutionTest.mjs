import test from 'node:test';
import assert from 'node:assert/strict';
import {jobSteps,renderJobExecution,jobCommand} from '../../apps/five-axis-cam/webpage/jobExecution.mjs';

function fixture(){
 const calls=[],events=[],domain={expected:{epoch:'epoch',mutationSequence:'3'},activeSetupId:'first',workpieces:[{id:'a',name:'甲<工件>'},{id:'b',name:'乙工件'}],placements:[{id:'pa',setupId:'first',workpieceId:'a',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]},{id:'pb',setupId:'second',workpieceId:'b',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],setups:[{id:'first',name:'工序一'},{id:'second',name:'工序二'}],intentToolpaths:[],fixtures:[],fixtureUnits:[]};
 const view={fiveAxisArea:'toolpath',fiveJobOpen:true,scene:{activePlacementId:'pa',domain,intentToolpaths:[]}};
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[]},isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/job.i5cam'},async autosave(){events.push('save');return {saved:true};}},actions:{async selectRibbonTab(id){events.push({tab:id});view.fiveAxisArea=id;}},sceneProxy:{async invoke(method,payload){calls.push({method,payload:structuredClone(payload)});if(method==='CamSetup.SetSequence')return {changed:true};if(method==='CamWorkpiece.Activate'){view.scene.activePlacementId=payload.placementId;domain.activeSetupId=domain.placements.find(p=>p.id===payload.placementId).setupId;return {changed:true};}throw Error('Unexpected method '+method);}}};
 const ops={renderProject(){},appendProjectLog(){},async refreshSceneState(){events.push('refresh');},async fitViewAfterRenderPublish(){}};
 return {view,context,ops,domain,calls,events};
}

test('job order is global across workpieces and sends the complete operation sequence to the native transaction',async()=>{
 const f=fixture(),geometry=structuredClone(f.domain.workpieces);assert.deepEqual(jobSteps(f.view).map(row=>row.workpiece.id),['a','b']);
 const html=renderJobExecution(f.view);assert.match(html,/aria-label="工序与输出"/);assert.match(html,/甲&lt;工件&gt;/);
 const down=html.match(/data-five-command="five.job.down:first"([^>]*)>/);assert.ok(down);assert.doesNotMatch(down[1],/disabled/,'A different workpiece must not block moving adjacent operations');
 await jobCommand(f.context,f.view,'down:first',f.ops);const call=f.calls.find(call=>call.method==='CamSetup.SetSequence');assert.ok(call);assert.deepEqual(call.payload.setupIds,['second','first']);assert.deepEqual(call.payload.expected,{epoch:'epoch',mutationSequence:'3'});assert.ok(call.payload.commandId);assert.deepEqual(f.domain.workpieces,geometry);assert.equal(f.events.filter(event=>event==='save').length,1);
});

test('editing an operation from the job dialog activates its referenced workpiece and opens Operation',async()=>{
 const f=fixture(),geometry=structuredClone(f.domain.workpieces);await jobCommand(f.context,f.view,'edit:second',f.ops);
 const call=f.calls.find(call=>call.method==='CamWorkpiece.Activate');assert.ok(call);assert.equal(call.payload.placementId,'pb');assert.equal(f.domain.activeSetupId,'second');assert.equal(f.view.scene.activePlacementId,'pb');assert.equal(f.view.fiveJobOpen,false);assert.ok(f.events.some(event=>event?.tab==='operation'));assert.deepEqual(f.domain.workpieces,geometry);
});
