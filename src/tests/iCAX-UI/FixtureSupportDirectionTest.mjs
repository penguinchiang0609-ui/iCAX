import assert from 'node:assert/strict';
import test from 'node:test';
import {designCommand,designDraft,designChanged,supportDirection} from '../../apps/five-axis-cam/webpage/fixtureDesign.mjs';
function fixture(){const calls=[],view={scene:{domain:{activeSetupId:'s',expected:{epoch:'e'},setups:[{id:'s',fixtureDesign:{}}],placements:[{id:'p',setupId:'s'}]}}};const ctx={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){calls.push({method,payload});if(method==='CamFixture.ReadSupportSelection')return {supportFaces:['5']};if(method==='CamPlacement.ReadReferenceFaces')return {faces:[]};return {status:'Completed',epoch:'e',taskId:'faces'};}}};return {view,ctx,calls,ops:{renderProject(){}}};}
test('manual direction edits remain drafts until explicit bulk addition',async()=>{
 const f=fixture();for(const [k,v] of [['supportX','3'],['supportY','4'],['supportZ','0']])designChanged(f.view,k,v);
 assert.deepEqual(supportDirection(designDraft(f.view)),[.6,.8,0]);assert.equal(f.calls.length,0);
 await designCommand(f.ctx,f.view,'auto',f.ops);f.calls[0].payload.supportDirection.forEach((n,i)=>assert(Math.abs(n-[.6,.8,0][i])<1e-12));assert.deepEqual(f.calls.map(c=>c.method),['CamFixture.FindSupportFaces','CamFixture.ReadSupportSelection','CamPlacement.ReadReferenceFaces']);
});
test('bulk additions union with manual edits; individual deletion and clear stay in the draft',async()=>{
 const f=fixture(),d=designDraft(f.view);d.supportFaces=['2','5'];
 await designCommand(f.ctx,f.view,'auto',f.ops);assert.deepEqual(d.supportFaces,['2','5']);assert.equal(f.calls.length,3);
 f.view.fixtureDesignSelectedFace='2';await designCommand(f.ctx,f.view,'remove-face:2',f.ops);assert.deepEqual(d.supportFaces,['5']);assert.equal(f.view.fixtureDesignSelectedFace,null);
 await designCommand(f.ctx,f.view,'clear',f.ops);assert.deepEqual(d.supportFaces,[]);assert.equal(f.calls.length,3);
 assert.deepEqual(supportDirection(d),[0,0,1]);
});
test('zero, empty and nonfinite directions cannot start recognition or generation',async()=>{
 for(const values of [['0','0','0'],['','0','1'],['Infinity','0','1']]){const f=fixture();for(const [i,k] of ['supportX','supportY','supportZ'].entries())designChanged(f.view,k,values[i]);for(const action of ['auto','generate'])await assert.rejects(designCommand(f.ctx,f.view,action,f.ops),/支撑方向/);assert.equal(f.calls.length,0);}
});
test('direction normalization accepts finite very large and small magnitudes',()=>{
 assert.deepEqual(supportDirection({supportX:'1e300',supportY:'0',supportZ:'0'}),[1,0,0]);assert.deepEqual(supportDirection({supportX:'0',supportY:'1e-300',supportZ:'0'}),[0,1,0]);
});
test('a lost fixture status response reads the same task again without generating twice',async()=>{
 const f=fixture();f.view.fiveFixtureDesignOpen=true;Object.assign(designDraft(f.view),{supportFaces:['5'],supportCount:'6',connectionCount:'2'});let reads=0;
 f.ctx.sceneProxy.invoke=async(method,payload,options)=>{
  f.calls.push({method,payload,options});
  if(method==='CamFixture.Generate')return {epoch:'e',taskId:'generation-1',status:'Running'};
  if(method==='CamFixture.QueryTask'){
   if(++reads===1)throw Object.assign(Error('Lost status response'),{name:'SDOTimeoutError'});
   return {epoch:'e',taskId:'generation-1',status:'Completed',result:{candidateId:'candidate-1'}};
  }
  if(method==='CamFixture.ReadDesign')return {recipe:{stations:[]},candidate:{id:'candidate-1'}};
  if(method==='CamFixture.QueryParts')return {items:[]};
  throw Error(`Unexpected ${method}`);
 };
 await designCommand(f.ctx,f.view,'generate',f.ops);
 assert.equal(f.calls.filter(c=>c.method==='CamFixture.Generate').length,1);
 assert.deepEqual(f.calls.filter(c=>c.method==='CamFixture.QueryTask').map(c=>c.payload),[{epoch:'e',taskId:'generation-1'},{epoch:'e',taskId:'generation-1'}]);
 assert.ok(f.calls.filter(c=>['CamFixture.Generate','CamFixture.QueryTask'].includes(c.method)).every(c=>c.options.timeoutMs>15000));assert.equal(f.view.pending,false);assert.equal(f.view.error,'');assert.equal(f.view.fiveFixtureGenerated.design.candidate.id,'candidate-1');
});
