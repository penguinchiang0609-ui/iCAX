import assert from 'node:assert/strict';
import test from 'node:test';
import {designDraft,designCommand,designChanged,designPick,renderFixtureDesign} from '../../apps/five-axis-cam/webpage/fixtureDesign.mjs';
import {fixtureCommand,fixtureFieldChanged} from '../../apps/five-axis-cam/webpage/fixtureActions.mjs';
import {fixtureDesignBusy} from '../../apps/five-axis-cam/webpage/fixtureDesignState.mjs';

const layout={supportCount:6,connectionCount:2,axis:'X',supportPositions:[-50,0,50],connectionPositions:[-30,30]};
function fixture(){
 const calls=[],requests=[],view={fiveAxisArea:'operation',fiveFixtureDesignOpen:true,fiveAxisUI:{tools:{operation:'fixture'}},scene:{activePlacementId:'p',model:{isLoaded:true},domain:{activeSetupId:'s',activePlacementId:'p',expected:{epoch:'e'},setups:[{id:'s',fixtureDesign:{}}],placements:[{id:'p',setupId:'s'}],fixtureUnits:[]}}};
 const d=designDraft(view);Object.assign(d,{supportFaces:['7'],dirty:true});
 const ctx={isCurrentProject:()=>true,sceneProxy:{invoke(method,payload){calls.push({method,payload});return new Promise((resolve,reject)=>requests.push({resolve,reject}));}}},ops={renderProject(){}};
 return {view,d,ctx,ops,calls,requests};
}
async function until(predicate){for(let i=0;i<50;i++){if(predicate())return;await new Promise(setImmediate);}assert.fail('The local fixture request did not settle');}
test('manual face recommendation visibly locks the panel and every mutation entry until success',async()=>{
 const f=fixture();f.view.fixtureDesignPicking=true;f.view.fixtureDesignPlacement='p';f.view.fixtureDesignFaces=[{faceId:'8',first:0,count:1}];
 assert.equal(designPick(f.ctx,f.view,{objectId:'p'},{faceIndex:0},null,null,f.ops),true);
 assert.equal(f.calls[0].method,'CamFixture.SuggestLayout');assert.equal(f.view.fiveFixtureDesignRequest.kind,'layout');assert.equal(fixtureDesignBusy(f.view),true);assert.equal(f.view.pending,undefined);
 const before=structuredClone(f.d),html=renderFixtureDesign(f.view);
 assert.match(html,/aria-busy="true"/);assert.match(html,/data-five-key="design-busy" role="status"[^>]*>正在计算支撑板数量…/);
 for(const input of html.matchAll(/<input\b[^>]*>/g))assert.match(input[0],/\breadonly\b/);
 for(const select of html.matchAll(/<select\b[^>]*>/g))assert.match(select[0],/\bdisabled\b/);
 for(const button of html.matchAll(/<button\b[^>]*>/g))if(!button[0].includes('design.cancel'))assert.match(button[0],/\bdisabled\b/);
 for(const summary of html.matchAll(/<summary\b[^>]*>/g))assert.match(summary[0],/aria-disabled="true" tabindex="-1"/);
 designChanged(f.view,'supportCount','9');fixtureFieldChanged(f.view,'design:material','Aluminum');designPick(f.ctx,f.view,{objectId:'p'},{faceIndex:0},null,null,f.ops);
 await designCommand(f.ctx,f.view,'generate',f.ops);await designCommand(f.ctx,f.view,'clear',f.ops);await fixtureCommand(f.ctx,f.view,'design.auto',f.ops);
 assert.deepEqual(f.d,before);assert.equal(f.calls.length,1);
 f.requests[0].resolve(layout);await until(()=>!f.view.fiveFixtureDesignRequest);
 assert.equal(f.d.supportCount,6);assert.equal(f.d.connectionCount,2);assert.equal(fixtureDesignBusy(f.view),false);assert.doesNotMatch(renderFixtureDesign(f.view),/data-five-key="design-busy"/);
 designChanged(f.view,'supportCount','9');assert.equal(f.d.supportCount,'9');
});
test('recommendation failure restores editing and retains the existing inline error',async()=>{
 const f=fixture(),request=designCommand(f.ctx,f.view,'layout-positions',f.ops);f.requests[0].reject(Error('Lost layout response'));await request;
 assert.equal(f.view.fiveFixtureDesignRequest,null);assert.equal(fixtureDesignBusy(f.view),false);assert.equal(f.view.error,'Lost layout response');assert.match(renderFixtureDesign(f.view),/role="alert">Lost layout response/);
});
test('a local layout request never releases an outer pending operation',async()=>{
 const f=fixture(),request=designCommand(f.ctx,f.view,'layout-positions',f.ops),progress={title:'Background refresh'};f.view.pending=true;f.view.progress=progress;
 f.requests[0].resolve(layout);await request;assert.equal(f.view.pending,true);assert.equal(f.view.progress,progress);assert.equal(f.view.fiveFixtureDesignRequest,null);
});
test('automatic face recognition keeps ownership of pending while its nested recommendation finishes',async()=>{
 const f=fixture(),renders=[];let releaseLayout;
 f.ops.renderProject=()=>renders.push({pending:f.view.pending,layout:!!f.view.fiveFixtureDesignRequest});
 f.ctx.sceneProxy.invoke=async method=>{
  if(method==='CamFixture.FindSupportFaces')return {epoch:'e',taskId:'auto',status:'Completed'};
  if(method==='CamFixture.ReadSupportSelection')return {supportFaces:['7']};
  if(method==='CamPlacement.ReadReferenceFaces')return {faces:[]};
  if(method==='CamFixture.SuggestLayout')return new Promise(resolve=>{releaseLayout=resolve;});
  throw Error('Unexpected '+method);
 };
 const request=designCommand(f.ctx,f.view,'auto',f.ops);await until(()=>!!releaseLayout);assert.equal(f.view.pending,true);assert.equal(f.view.fiveFixtureDesignRequest.kind,'layout');
 releaseLayout(layout);await request;assert.ok(renders.some(state=>state.pending&&!state.layout),'The nested request releases only its own busy state before the outer finally');assert.equal(f.view.pending,false);assert.equal(f.view.fiveFixtureDesignRequest,null);assert.equal(f.d.supportCount,6);
});
for(const change of ['setup','epoch','project'])for(const response of ['success','error'])test(`a stale ${change} layout ${response} cannot change the draft or current error`,async()=>{
 const f=fixture(),before=structuredClone(f.d),request=designCommand(f.ctx,f.view,'layout-positions',f.ops);
 if(change==='setup')f.view.scene.domain.activeSetupId='other';else if(change==='epoch')f.view.scene.domain.expected.epoch='other';else f.ctx.isCurrentProject=()=>false;
 f.view.error='New context error';if(response==='success')f.requests[0].resolve(layout);else f.requests[0].reject(Error('Old layout error'));await request;assert.deepEqual(f.d,before);assert.equal(f.view.error,'New context error');assert.equal(f.view.fiveFixtureDesignRequest,null);
});
test('cancel and reopen release only the old wrapper and ignore its late error',async()=>{
 const f=fixture(),old=fixtureCommand(f.ctx,f.view,'design.layout-positions',f.ops);await until(()=>f.requests.length===1);const oldWrapper=f.view.fixtureRequest;
 const progress={title:'Unrelated refresh'};f.view.pending=true;f.view.progress=progress;
 await fixtureCommand(f.ctx,f.view,'design.cancel',f.ops);assert.equal(f.view.fiveFixtureDesignRequest,null);assert.equal(f.view.fixtureRequest,null);assert.equal(f.view.pending,true);assert.equal(f.view.progress,progress);
 f.view.pending=false;f.view.progress=null;f.view.fiveFixtureDesignOpen=true;const fresh=designDraft(f.view);Object.assign(fresh,{supportFaces:['9'],dirty:true});
 const next=fixtureCommand(f.ctx,f.view,'design.layout-positions',f.ops);await until(()=>f.requests.length===2);const nextWrapper=f.view.fixtureRequest,nextState=f.view.fiveFixtureDesignRequest;assert.notEqual(nextWrapper,oldWrapper);
 f.view.error='New editor';f.requests[0].reject(Error('Old canceled request'));await old;
 assert.equal(f.view.fixtureRequest,nextWrapper);assert.equal(f.view.fiveFixtureDesignRequest,nextState);assert.equal(f.view.error,'New editor');assert.equal(fresh.supportCount,'');
 f.requests[1].resolve(layout);await next;assert.equal(f.view.fixtureRequest,null);assert.equal(f.view.fiveFixtureDesignRequest,null);assert.equal(fresh.supportCount,6);
});
test('manual face loading has the same busy and cancellation fence',async()=>{
 const f=fixture(),request=fixtureCommand(f.ctx,f.view,'design.pick',f.ops);await until(()=>f.requests.length===1);
 assert.equal(f.view.fiveFixtureDesignRequest.kind,'faces');assert.match(renderFixtureDesign(f.view),/正在读取承托面…/);
 await fixtureCommand(f.ctx,f.view,'design.cancel',f.ops);f.requests[0].resolve({faces:[{faceId:'8'}]});await request;
 assert.equal(f.view.fiveFixtureDesignOpen,false);assert.equal(f.view.fixtureDesignPicking,false);assert.equal(f.view.fiveFixtureDesignRequest,null);assert.equal(f.view.fixtureRequest,null);
});
test('an old generated candidate task does not own pending during local layout cancellation',async()=>{
 const f=fixture(),request=designCommand(f.ctx,f.view,'layout-positions',f.ops);f.view.fiveFixtureGenerated={task:{epoch:'e',taskId:'completed-preview',status:'Completed'}};
 const progress={title:'Unrelated refresh'};f.view.pending=true;f.view.progress=progress;
 const originalInvoke=f.ctx.sceneProxy.invoke;f.ctx.sceneProxy.invoke=(method,payload)=>method==='CamFixture.CancelTask'?Promise.resolve({}):originalInvoke(method,payload);
 await designCommand(f.ctx,f.view,'cancel',f.ops);assert.equal(f.view.pending,true);assert.equal(f.view.progress,progress);f.requests[0].resolve(layout);await request;assert.equal(f.view.fiveFixtureDesignRequest,null);
});
