import test from 'node:test';
import assert from 'node:assert/strict';
import {flushCollisionThreshold,scheduleCollisionThreshold} from '../../apps/five-axis-cam/webpage/collisionThreshold.mjs';
function fixture(){
 let domain={expected:{epoch:'epoch',mutationSequence:'0'},activeSetupId:'setup',placements:[],workpieces:[],fixtures:[],fixtureUnits:[],intentToolpaths:[],setups:[{id:'setup',rapidSettings:{collisionThresholdMm:.1},generatedGeometry:{status:'Missing'}}]},calls=[],saves=0,gate=null;
 const view={pending:false,scene:{domain:structuredClone(domain)},fiveSimulation:{}},ops={renderProject(){},appendProjectLog(){}};
 const context={isCurrentProject:()=>true,projectProxy:{state:{projectPath:'C:/test/collision.i5cam'},async autosave(){saves++;}},sceneProxy:{async invoke(method,payload){
  if(method==='CamRapid.Configure'){calls.push(structuredClone(payload));if(gate)await gate;domain.expected.mutationSequence=String(Number(domain.expected.mutationSequence)+1);domain.setups[0].rapidSettings=structuredClone(payload.settings);return {changed:true};}
  if(method==='CamScene.ReadState')return structuredClone(domain);throw Error(method);
 }}};
 return {view,context,ops,calls,get saves(){return saves;},hold(){let release;gate=new Promise(r=>release=r);return ()=>{gate=null;release();};}};
}
test('editing a valid threshold saves it automatically without disabling the focused field',async()=>{
 const f=fixture();f.view.fiveSimulation.thresholdDraft='.25';scheduleCollisionThreshold(f.context,f.view,f.ops);
 await new Promise(r=>setTimeout(r,400));await flushCollisionThreshold(f.context,f.view,f.ops);
 assert.equal(f.calls.length,1);assert.equal(f.calls[0].settings.collisionThresholdMm,.25);assert.equal(f.saves,1);assert.equal(f.view.pending,false);assert.equal(f.view.scene.domain.setups[0].rapidSettings.collisionThresholdMm,.25);
});
test('a later edit during saving is serialized and retains the newest value',async()=>{
 const f=fixture(),release=f.hold();f.view.fiveSimulation.thresholdDraft='.2';const pending=flushCollisionThreshold(f.context,f.view,f.ops);
 assert.equal(f.view.pending,false);f.view.fiveSimulation.thresholdDraft='.3';release();assert.equal(await pending,true);
 assert.deepEqual(f.calls.map(c=>c.settings.collisionThresholdMm),[.2,.3]);assert.equal(f.calls[1].expected.mutationSequence,'1');assert.equal(f.view.scene.domain.setups[0].rapidSettings.collisionThresholdMm,.3);
});
test('unchanged, invalid and departed drafts never create a setting or undo step',async()=>{
 const f=fixture();f.view.fiveSimulation.thresholdDraft='0.1';assert.equal(await flushCollisionThreshold(f.context,f.view,f.ops),true);assert.equal(f.calls.length,0);
 for(const value of ['', '-1','0.','wrong']){f.view.fiveSimulation.thresholdDraft=value;assert.equal(await flushCollisionThreshold(f.context,f.view,f.ops),false);}assert.equal(f.calls.length,0);
 f.view.fiveSimulation.thresholdDraft='.4';f.context.isCurrentProject=()=>false;assert.equal(await flushCollisionThreshold(f.context,f.view,f.ops),false);assert.equal(f.calls.length,0);
});
