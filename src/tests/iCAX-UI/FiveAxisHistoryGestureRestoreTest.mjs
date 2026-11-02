import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {TransformManipulator} from '../../iCAX-UI/SDK/Viewport/transformManipulator.mjs';
import {syncMachineIK,finishMachineIK,disposeMachineIK} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

// Entry's bindings are private. Execute its complete exported handler directly
// from source with dependency injection, retaining the real machine sync and
// TransformControls capture path instead of duplicating its finally block.
const entry=readFileSync(new URL('../../apps/five-axis-cam/webpage/entry.mjs',import.meta.url),'utf8');
const source=entry.slice(entry.indexOf('export async function handleProjectHistoryCommand('),entry.indexOf('export async function onProjectHistoryChanged(')).replace(/^export /,'');
const turn=()=>new Promise(resolve=>setImmediate(resolve));

function fixture(t){
 const previousFrame=globalThis.requestAnimationFrame,previousCancel=globalThis.cancelAnimationFrame;
 globalThis.requestAnimationFrame=callback=>setTimeout(callback,1);globalThis.cancelAnimationFrame=clearTimeout;
 const doc=new EventTarget();doc.defaultView=new EventTarget();doc.activeElement=null;
 let active=true,finalSyncs=0,rapidSyncs=0,focuses=0,historyRefreshes=0,renderCalls=0,releaseAction,enteredAction;
 const captures=new Set(),canvas={style:{},clientWidth:800,clientHeight:600,closest:()=>null,getBoundingClientRect:()=>({left:0,top:0,width:800,height:600}),focus(){focuses++;},setPointerCapture:id=>captures.add(id),hasPointerCapture:id=>captures.has(id),releasePointerCapture:id=>captures.delete(id),removeEventListener(){}};
 const camera=new THREE.PerspectiveCamera(45,800/600,.1,2000);camera.position.set(200,-250,200);camera.lookAt(0,0,0);camera.updateMatrixWorld(true);
 const object=new THREE.Object3D(),model={entityId:'machine',enabled:true,elements:[{entityId:'tip',kind:'part',parentEntityId:'machine'}],joints:[{entityId:'tip',type:'prismatic',axis:[1,0,0],position:0,lower:-100,upper:100}]};
 const view={scene:{machines:[model],intentToolpaths:[],domain:{expected:{epoch:'history',mutationSequence:'0'},activeSetupId:'setup',setups:[{id:'setup',locked:true}],placements:[],workpieces:[]}},fiveAxisArea:'simulation',fiveMachine:{machineId:'machine'},fiveMachineSelectedPartId:'tip',fiveMachineIK:{machineId:'machine',tip:'tip',mode:'translate',motionMode:'IK'},activeAreaId:'main',areas:{main:{viewReader:{setPollingPaused(){},setPollInterval(){},async poll(){}}}}};selectTool(view,'machine-instances');
 const vp=view.viewport={scene:new THREE.Scene(),camera,renderer:{domElement:canvas},sceneObjects:new Map([['tip',object]]),setTransformPreviews(){},clearTransformPreviews(){},
  setTransformManipulator(options){this.transformManipulator??=new TransformManipulator(this,()=>this.transformManipulator?.update());this.transformManipulator.configure(options);},clearTransformManipulator(){const m=this.transformManipulator;this.transformManipulator=null;m?.dispose();}};
 const mount={ownerDocument:doc,querySelector:()=>null,querySelectorAll:()=>[]},bindings=new WeakMap();
 const context={mount,project:{projectId:'history'},isCurrentProject:()=>active,actions:{refreshProjectHistoryControls(){historyRefreshes++;},async refreshNativeProjectHistory(){}},sceneProxy:{pdo:{enabled:true,async withReadDescriptor(){return null;},async withWriteDescriptor(){return true;}},async invoke(method,request){
  if(method==='CamSimulation.BeginMachinePoseEdit')return {editToken:request.editToken,epoch:'history',expected:{epoch:'history',mutationSequence:'0'},motionMode:'IK',jointIds:['tip'],positions:[0],worldIds:['tip'],worldTransformIds:['tip'],tipWorld:new THREE.Matrix4().toArray(),targetPDO:{id:'target',version:1,payloadSize:4096},resultPDO:{id:'result',version:1,payloadSize:4096}};
  assert.equal(method,'CamSimulation.EndMachinePoseEdit');return {pending:false,changed:false,cancelled:true,expected:{epoch:'history',mutationSequence:'0'}};
 }}};
 const ops={renderProject(){renderCalls++;if(active&&!view.disposed)syncMachineIK(context,view,ops);},async refreshSceneState(){return true;}};
 const binding={context,view,ops};bindings.set(mount,binding);
 const action=async()=>{assert.equal(view.fiveHistoryBusy,true);syncMachineIK(context,view,ops);assert.equal(vp.transformManipulator.control.enabled,false);enteredAction?.();await new Promise(resolve=>releaseAction=resolve);};context.actions.undoProject=action;context.actions.redoProject=action;
 const dependencies={getProjectView:()=>view,bindings,getProjectHistoryState:()=>({blocked:false}),exitToolAxisPointModes(){},floatingTransformDraft:()=>false,hasUnappliedWorkpieceDrafts:()=>false,hasUnappliedCutDrafts:()=>false,async cancelInvalidRapidInput(){return false;},cancelInvalidMachineParameters:()=>false,async stopMachineJog(){},stopMachine(){},async flushMachineParameterEdits(){},async flushRapidPathSave(){},async flushMachinePoseEdits(){},async flushMachineColliderSaves(){},cancelInvalidHeadColliderEdits:()=>false,async flushHeadColliderSaves(){},async flushAlignmentEdits(){return true;},async flushEditedProjectSave(){},syncRapidPathEditing(){rapidSyncs++;},syncMachineIK(...args){finalSyncs++;syncMachineIK(...args);}};
 const handle=Function(...Object.keys(dependencies),source+'\nreturn handleProjectHistoryCommand;')(...Object.values(dependencies));
 syncMachineIK(context,view,ops);
 const f={view,context,vp,bindings,mount,binding,ops,handle,canvas,captures,get finalSyncs(){return finalSyncs;},get rapidSyncs(){return rapidSyncs;},get focuses(){return focuses;},get renderCalls(){return renderCalls;},get historyRefreshes(){return historyRefreshes;},setActive(value){active=value;},release(){releaseAction();},async start(direction){const reached=new Promise(resolve=>enteredAction=resolve),task=handle(context,direction);await reached;return {task};}};
 t.after(async()=>{releaseAction?.();disposeMachineIK(view);await finishMachineIK(view);globalThis.requestAnimationFrame=previousFrame;globalThis.cancelAnimationFrame=previousCancel;});return f;
}

test('undo and redo re-enable the existing machine controller after busy clears and allow a fresh real capture',async t=>{
 for(const direction of ['undo','redo']){
  const f=fixture(t),m=f.vp.transformManipulator,{task}=await f.start(direction);assert.equal(m.control.enabled,false);assert.equal(f.finalSyncs,0);
  f.release();assert.equal(await task,true);assert.equal(f.view.fiveHistoryBusy,false);assert.equal(f.vp.transformManipulator,m);assert.equal(m.control.enabled,true);assert.equal(f.finalSyncs,1);assert.equal(f.rapidSyncs,1);assert.equal(f.renderCalls,0,'completion must use local sync rather than reconstruct the workbench');
  m.hover=()=>{m.control.axis='X';return true;};const event={button:0,buttons:1,clientX:420,clientY:300,pointerId:7,preventDefault(){}};assert.equal(m.down(event),true);assert.equal(m.pointerId,7);assert.equal(f.captures.has(7),true);m.finish(true);await finishMachineIK(f.view);
 }
});

test('history completion after switching project does not restore the old controller or focus',async t=>{
 const f=fixture(t),m=f.vp.transformManipulator,{task}=await f.start('undo');f.setActive(false);const previousFocus=f.focuses;f.release();await task;
 assert.equal(f.view.fiveHistoryBusy,false);assert.equal(f.finalSyncs,0);assert.equal(f.rapidSyncs,0);assert.equal(m.control.enabled,false);assert.equal(f.focuses,previousFocus);assert.equal(f.renderCalls,0);assert.ok(f.historyRefreshes>0);
});

test('a replaced mount binding fences a late redo even if the old context still reports current',async t=>{
 const f=fixture(t),{task}=await f.start('redo');f.bindings.set(f.mount,{context:{},view:{},ops:{}});const previousFocus=f.focuses;f.release();await task;
 assert.equal(f.view.fiveHistoryBusy,false);assert.equal(f.finalSyncs,0);assert.equal(f.rapidSyncs,0);assert.equal(f.focuses,previousFocus);assert.equal(f.renderCalls,0);
});

test('a disposed view clears busy without reviving its gizmo after an asynchronous history command',async t=>{
 const f=fixture(t),{task}=await f.start('undo');f.view.disposed=true;f.release();await task;assert.equal(f.view.fiveHistoryBusy,false);assert.equal(f.finalSyncs,0);assert.equal(f.rapidSyncs,0);assert.equal(f.renderCalls,0);
});
