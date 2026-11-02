import assert from 'node:assert/strict';
import test from 'node:test';
import {designCommand,designDraft} from '../../apps/five-axis-cam/webpage/fixtureDesign.mjs';
const completed={status:'Completed',epoch:'e',taskId:'selection',result:{selectionId:'selection',selectedFaceCount:0}};

function fixture(invoke){
 const calls=[],view={scene:{domain:{activeSetupId:'s',expected:{epoch:'e',revision:'1'},setups:[{id:'s',fixtureDesign:{}}],placements:[{id:'p',setupId:'s'}]}}};
 const ctx={isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){calls.push(method);return invoke(method,payload,view);}}};
 designDraft(view).supportFaces=['previous'];
 return {view,ctx,calls,ops:{renderProject(){}}};
}
for(const [name,response,error] of [
 ['cancellation',{status:'Cancelled'},''],
 ['superseded input',{status:'Superseded'},'识别期间工件发生变化'],
 ['empty suggestion',completed,'未找到沿当前支撑方向可接触'],
 ['worker failure',{status:'Failed',error:'PLATE_MESH_LIMIT'},'超出夹具计算容量']
])test(`${name} keeps the existing editable selection and releases its operation`,async()=>{
 const f=fixture(method=>method==='CamFixture.ReadSupportSelection'?{supportFaces:[]}:method==='CamPlacement.ReadReferenceFaces'?{faces:[]}:response);await designCommand(f.ctx,f.view,'auto',f.ops);
 assert.deepEqual(designDraft(f.view).supportFaces,['previous']);assert.equal(f.view.pending,false);assert.equal(f.view.progress,null);
 if(error)assert(f.view.error.includes(error));else assert.equal(f.view.error,'');
 assert.deepEqual(f.calls,['CamFixture.FindSupportFaces',...(response.status==='Completed'?['CamFixture.ReadSupportSelection','CamPlacement.ReadReferenceFaces']:[])]);
});
test('lost completed recognition response recovers the task result without generating or saving',async()=>{
 const f=fixture((method,payload)=>{if(method==='CamFixture.FindSupportFaces')throw Error('lost');if(method==='CamScene.QueryCommand')return {...completed,result:{selectionId:'selection',selectedFaceCount:1}};if(method==='CamFixture.ReadSupportSelection'){assert.deepEqual(payload,{epoch:'e',taskId:'selection'});return {supportFaces:['9']};}if(method==='CamPlacement.ReadReferenceFaces')return {faces:[{faceId:'9',first:'0',count:'2'}]};throw Error(method);});
 await designCommand(f.ctx,f.view,'auto',f.ops);assert.equal(f.view.error,'');assert.deepEqual(designDraft(f.view).supportFaces,['previous','9']);assert.deepEqual(f.view.fixtureDesignFaces,[{faceId:'9',first:'0',count:'2'}]);
 assert.deepEqual(f.calls,['CamFixture.FindSupportFaces','CamScene.QueryCommand','CamFixture.ReadSupportSelection','CamPlacement.ReadReferenceFaces']);
});
test('an older recognition result cannot replace a newer draft',async()=>{
 const f=fixture((_method,_payload,view)=>{const d=designDraft(view);d.supportFaces=['newer'];d.dirty=true;++d.revision;return completed;});
 await designCommand(f.ctx,f.view,'auto',f.ops);assert.deepEqual(designDraft(f.view).supportFaces,['newer']);assert.equal(f.view.pending,false);
});
