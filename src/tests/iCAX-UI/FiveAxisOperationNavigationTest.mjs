import test from 'node:test';
import assert from 'node:assert/strict';
import {prepareOperationNavigation} from '../../apps/five-axis-cam/webpage/operationNavigation.mjs';

const deferred=()=>{let resolve;const promise=new Promise(done=>{resolve=done;});return {promise,resolve};};
const turn=()=>new Promise(resolve=>setTimeout(resolve,0));
async function until(predicate){for(let i=0;i<200&&!predicate();i++)await turn();assert.ok(predicate(),'The expected native preparation phase did not start');}

function fixture(t,{onActivate,onSave,onRead}={}){
  const oldCancel=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
  t.after(()=>{globalThis.cancelAnimationFrame=oldCancel;});
  const native={expected:{epoch:'navigation',mutationSequence:'1'},activePlacementId:'operation-placement',activeSetupId:'setup',
    workpieces:[{id:'workpiece',name:'Workpiece',enabled:true,geometryRef:{url:'resource://workpiece'}}],
    setups:[{id:'setup',enabled:true}],placements:[{id:'source',setupId:'',workpieceId:'workpiece'},{id:'operation-placement',setupId:'setup',workpieceId:'workpiece'}],
    fixtures:[],fixtureUnits:[],intentToolpaths:[]};
  const view={fiveAxisArea:'operation',activeAreaId:'operation',fivePreparationKind:'setup',fiveAxisUI:{tools:{workpiece:'select',operation:'machine-type'}},
    scene:{domain:structuredClone(native),activePlacementId:'operation-placement'}};
  const events=[],activations=[];let current=true,saveCount=0,readCount=0;
  const event=(phase,extra={})=>events.push({phase,preparing:!!view.fiveOperationNavigationPreparing,navigationPending:!!view.fiveOperationNavigationPending,...extra});
  const context={isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/navigation.i5cam'},async autosave(){
    const index=saveCount++;event('save-start',{index});await onSave?.(index);event('save-end',{index});return {saved:true};
  }},sceneProxy:{async invoke(method,payload){
    assert.equal(method,'CamWorkpiece.Activate');const index=activations.length;activations.push(payload.placementId);event('activate-start',{index,id:payload.placementId});
    await onActivate?.(payload.placementId,index);
    const target=native.placements.find(p=>p.id===payload.placementId);native.activePlacementId=target.id;native.activeSetupId=target.setupId;
    native.expected.mutationSequence=String(Number(native.expected.mutationSequence)+1);event('activate-end',{index,id:target.id});return {};
  }}};
  const ops={renderProject(){event('render');},async refreshSceneState(){
    const index=readCount++;event('read-start',{index});await onRead?.(index);
    if(current&&!view.disposed){view.scene.domain=structuredClone(native);view.scene.activePlacementId=native.activePlacementId;}
    event('read-end',{index});return true;
  },async fitViewAfterRenderPublish(){event('fit');}};
  return {view,native,context,ops,events,activations,request:area=>prepareOperationNavigation(context,view,ops,area),leave(){current=false;}};
}

test('an in-flight Activate drains once; superseded queued pages never activate another placement',async t=>{
  const gate=deferred(),f=fixture(t,{onActivate:(_id,index)=>index===0?gate.promise:undefined});
  const first=f.request('workpiece');await until(()=>f.activations.length===1);
  const middle=f.request('toolpath'),latest=f.request('workpiece');
  assert.equal(f.view.fiveOperationNavigationPending,true);assert.equal(f.view.fiveOperationNavigationPreparing,true);
  gate.resolve();assert.deepEqual(await Promise.all([first,middle,latest]),[false,false,true]);
  assert.deepEqual(f.activations,['source']);assert.equal(f.view.scene.activePlacementId,'source');
  assert.equal(f.view.fiveOperationNavigationPending,false);assert.equal(f.view.fiveOperationNavigationPreparing,false);
});

test('clicking the still-active operation page cancels a pending workpiece navigation and restores its actual placement',async t=>{
  const gate=deferred(),f=fixture(t,{onActivate:(_id,index)=>index===0?gate.promise:undefined});
  const leaving=f.request('workpiece');await until(()=>f.activations.length===1);
  // The shell has not committed the first preflight, so operation is still
  // the selected tab when the user clicks it again.
  assert.equal(f.view.activeAreaId,'operation');const returning=f.request('operation');gate.resolve();
  assert.deepEqual(await Promise.all([leaving,returning]),[false,true]);
  assert.deepEqual(f.activations,['source','operation-placement']);assert.equal(f.view.scene.activePlacementId,'operation-placement');
  assert.equal(f.events.some(event=>event.phase==='fit'),false,'A canceled page preparation must preserve the current camera');
});

test('Activate, archive save and delayed readback stay serial across a later page request',async t=>{
  const save=deferred(),read=deferred(),lastActivate=deferred();
  const f=fixture(t,{onSave:index=>index===0?save.promise:undefined,onRead:index=>index===0?read.promise:undefined,onActivate:(_id,index)=>index===1?lastActivate.promise:undefined});
  const first=f.request('workpiece');await until(()=>f.events.some(e=>e.phase==='save-start'));
  const latest=f.request('toolpath');assert.deepEqual(f.activations,['source']);
  assert.equal(f.view.fiveOperationNavigationPreparing,true);save.resolve();await until(()=>f.events.some(e=>e.phase==='read-start'));
  assert.deepEqual(f.activations,['source']);read.resolve();await until(()=>f.activations.length===2);
  assert.equal(await first,false);assert.equal(f.view.fiveOperationNavigationPending,true,'An earlier finally must not clear the latest queued owner');
  lastActivate.resolve();assert.equal(await latest,true);
  const firstReadEnd=f.events.findIndex(e=>e.phase==='read-end'&&e.index===0),secondActivate=f.events.findIndex(e=>e.phase==='activate-start'&&e.index===1);
  assert.ok(firstReadEnd<secondActivate);assert.equal(f.view.scene.activePlacementId,'operation-placement');
  assert.ok(f.events.filter(e=>/^(activate|save|read)-/.test(e.phase)).every(e=>e.preparing&&e.navigationPending));
  assert.equal(f.view.fiveOperationNavigationPending,false);assert.equal(f.view.fiveOperationNavigationPreparing,false);
});

for(const flag of ['fiveOperationAreaSync','fiveWorkpieceSwitching','sceneRefreshPending'])test(`navigation waits for existing ${flag} without owning its progress or losing the latest target`,async t=>{
  const f=fixture(t);f.view[flag]=true;f.view.pending=true;
  const first=f.request('toolpath');await turn();const latest=f.request('workpiece');await turn();
  assert.deepEqual(f.activations,[]);assert.equal(f.view.fiveOperationNavigationPreparing,undefined);
  assert.equal(f.view.fiveOperationNavigationPending,true);f.view[flag]=false;f.view.pending=false;
  assert.deepEqual(await Promise.all([first,latest]),[false,true]);assert.deepEqual(f.activations,['source']);
  assert.equal(f.view.fiveOperationNavigationPending,false);
});

for(const flag of ['pending','fixtureRequest','fiveGeneration'])test(`unrelated ${flag} work keeps its existing navigation guard`,async t=>{
  const f=fixture(t);f.view[flag]=flag==='fiveGeneration'?{request:{commandId:'generating'}}:true;
  assert.equal(await f.request('workpiece'),false);assert.deepEqual(f.activations,[]);
  assert.equal(f.view.fiveOperationNavigationPreparing,undefined);assert.equal(f.view.fiveOperationNavigationPending,false);
});

test('a failed Activate retains its error, clears ownership and allows an explicit later retry',async t=>{
  const f=fixture(t,{onActivate:(_id,index)=>{if(index===0)throw Error('Activation failed');}});
  assert.equal(await f.request('workpiece'),false);assert.equal(f.view.error,'Activation failed');
  assert.equal(f.view.fiveOperationNavigationPending,false);assert.equal(f.view.fiveOperationNavigationPreparing,false);
  assert.equal(await f.request('workpiece'),true);assert.equal(f.view.error,'');
});

test('a successful latest page leaves the operation-create dialog only for the operation page',async t=>{
  const f=fixture(t),dialog={step:'machine'};f.view.fiveOperationCreateDialog=dialog;
  assert.equal(await f.request('operation'),true);assert.equal(f.view.fiveOperationCreateDialog,dialog);
  assert.equal(await f.request('help'),true);assert.equal(f.view.fiveOperationCreateDialog,null);
  assert.deepEqual(f.activations,[]);
});

for(const departure of ['project','dispose'])test(`${departure} departure cancels a queued intent without a late navigation result`,async t=>{
  const gate=deferred(),f=fixture(t,{onActivate:(_id,index)=>index===0?gate.promise:undefined});
  const first=f.request('workpiece');await until(()=>f.activations.length===1);const latest=f.request('operation');
  if(departure==='project')f.leave();else f.view.disposed=true;
  const renderCount=f.events.filter(e=>e.phase==='render').length;gate.resolve();
  assert.deepEqual(await Promise.all([first,latest]),[false,false]);assert.deepEqual(f.activations,['source']);
  assert.equal(f.events.filter(e=>e.phase==='render').length,renderCount);
  assert.equal(f.view.fiveOperationNavigationPending,false);assert.equal(f.view.fiveOperationNavigationPreparing,false);
});

test('an unbound mount still invalidates an older preparation but never starts native work',async t=>{
  const gate=deferred(),f=fixture(t,{onActivate:(_id,index)=>index===0?gate.promise:undefined});
  const first=f.request('workpiece');await until(()=>f.activations.length===1);
  const latest=prepareOperationNavigation(f.context,f.view,null,'help');gate.resolve();
  assert.deepEqual(await Promise.all([first,latest]),[false,true]);assert.deepEqual(f.activations,['source']);
  assert.equal(f.view.fiveOperationNavigationPending,false);
});
