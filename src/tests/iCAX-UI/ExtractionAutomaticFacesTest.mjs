import assert from 'node:assert/strict';
import test from 'node:test';
import {extractionCommand,extractionDraft,extractionChanged} from '../../apps/five-axis-cam/webpage/extractionActions.mjs';

const completed={status:'Completed',epoch:'epoch',taskId:'selection',result:{selectionId:'selection',selectedFaceCount:2}};
const faceUses=['existing','one','two'].map(localId=>({kind:'Face',localId}));
const faces=[{faceId:'one',first:0,count:2},{faceId:'one',first:5,count:3},{faceId:'two',first:8,count:4}];
function fixture(overrides={}){
 const view={fiveAxisExtractionOpen:true,scene:{activePlacementId:'p',model:{geometryRef:{id:'geometry',revision:'1'}},domain:{expected:{epoch:'epoch',mutationSequence:'0'},activeSetupId:'s',placements:[{id:'p',setupId:'s',workpieceId:'w',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],workpieces:[{id:'w'}],setups:[{id:'s'}]}}};
 const state=extractionDraft(view).extraction;state.faces=[faceUses[0]];state.faceRanges={existing:[{first:20,count:2}]};
 const calls=[],context={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){
  calls.push({method,payload});if(overrides[method])return overrides[method](payload);
  switch(method){
   case 'CamCut.FindRetainedFaces':case 'CamPlan.QueryStatus':return completed;
   case 'CamCut.ReadRetainedSelection':return {faceIds:['one','two']};
   case 'CamCut.QuerySources':return {faceUses};
   case 'CamPlacement.ReadReferenceFaces':return {faces};
   default:throw Error('Unexpected method '+method);
  }
 }}};
 const ops={renderProject(){},appendProjectLog(){}};
 return {view,state,calls,context,run:action=>extractionCommand(context,view,action,null,ops)};
}
for(const running of [false,true])test(`batch selection queries data after ${running?'polled':'immediate'} receipt and retains manual faces`,async()=>{
 const f=fixture(running?{'CamCut.FindRetainedFaces':()=>({...completed,status:'Running',result:undefined})}:{}),before=structuredClone(f.view.scene);
 await f.run('auto');assert.equal(f.view.error,'');assert.equal(f.view.pending,false);assert.equal(f.state.faceRequest,null);
 assert.deepEqual(f.state.faces.map(f=>f.localId),['existing','one','two']);
 assert.deepEqual(f.state.faceRanges,{existing:[{first:20,count:2}],one:[{first:0,count:2},{first:5,count:3}],two:[{first:8,count:4}]});
 assert.deepEqual(f.calls.map(c=>c.method),['CamCut.FindRetainedFaces',...(running?['CamPlan.QueryStatus']:[]),'CamCut.ReadRetainedSelection','CamCut.QuerySources','CamPlacement.ReadReferenceFaces']);
 assert.deepEqual(f.calls.find(c=>c.method==='CamCut.ReadRetainedSelection').payload,{epoch:'epoch',taskId:'selection'});
 assert.deepEqual(f.view.scene,before,'Selection must not alter CAD, fixtures or paths');
 await f.run('auto');assert.equal(f.state.faces.length,3,'Repeated batch add must not duplicate faces');
});
for(const status of ['Cancelled','Superseded','Failed'])test(`${status} task does not query or replace selection`,async()=>{
 const f=fixture({'CamCut.FindRetainedFaces':()=>({...completed,status})});await f.run('auto');
 assert.deepEqual(f.state.faces,[faceUses[0]]);assert.equal(f.view.pending,false);assert.equal(f.calls.length,1);
 assert.equal(!!f.view.error,status!=='Cancelled');
});
test('CAM retained-face direction stays in workpiece coordinates under a rotated placement',async()=>{
 const f=fixture();f.view.scene.domain.placements[0].transform=[0,0,1,100,0,1,0,20,-1,0,0,30,0,0,0,1];
 f.state.observation=['0','0','-2'];await f.run('auto');
 assert.deepEqual(f.calls[0].payload.observationDirection,[0,0,-1]);assert.equal(f.view.error,'');
});
for(const boundary of ['cancel','close','revision','project','proxy'])test(`late selection query cannot apply after ${boundary}`,async()=>{
 let release,entered;const waiting=new Promise(r=>entered=r);
 const f=fixture({'CamCut.ReadRetainedSelection':()=>{entered();return new Promise(r=>release=()=>r({faceIds:['one','two']}));}});
 const pending=f.run('auto');await waiting;
 if(boundary==='cancel'||boundary==='close')await f.run(boundary);
 if(boundary==='revision')extractionChanged(f.view);
 if(boundary==='project')f.context.isCurrentProject=()=>false;
 if(boundary==='proxy')f.context.sceneProxy={invoke(){throw Error('Must not use a different scene');}};
 const before=structuredClone(f.state.faces);release();await pending;
 assert.deepEqual(f.state.faces,before);assert.equal(f.view.pending,false);assert.equal(f.state.faceRequest,null);
});
test('a task started before switching scenes is cancelled through its original scene',async()=>{
 let release,entered;const waiting=new Promise(r=>entered=r);
 const f=fixture({'CamCut.FindRetainedFaces':()=>{entered();return new Promise(r=>release=()=>r({...completed,status:'Running'}));},'CamPlan.CancelTask':()=>({status:'Cancelled'})});
 const pending=f.run('auto');await waiting;
 f.context.sceneProxy={invoke(){throw Error('Must not cancel through the new scene');}};
 release();await pending;
 assert.deepEqual(f.calls.map(c=>c.method),['CamCut.FindRetainedFaces','CamPlan.CancelTask']);
 assert.deepEqual(f.state.faces,[faceUses[0]]);assert.equal(f.view.pending,false);
});
for(const kind of ['empty','query failure','ambiguous reference'])test(`${kind} leaves manual selection intact with a useful error`,async()=>{
 const overrides=kind==='empty'?{'CamCut.ReadRetainedSelection':()=>({faceIds:[]})}:kind==='query failure'?{'CamCut.ReadRetainedSelection':()=>{throw Error('选面结果读取失败');}}:{'CamCut.QuerySources':()=>({faceUses:[...faceUses,faceUses[1]]})};
 const f=fixture(overrides),before=structuredClone(f.state.faceRanges);await f.run('auto');
 assert.deepEqual(f.state.faces,[faceUses[0]]);assert.deepEqual(f.state.faceRanges,before);assert.equal(f.view.pending,false);
 assert.match(f.view.error,kind==='empty'?/未找到可选表面/:kind==='query failure'?/选面结果读取失败/:/多重拓扑引用/);
});
