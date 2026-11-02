import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {openRapid,rapidPick,rapidCommand,saveRapidPathTopology,flushRapidPathSave,scheduleRapidPathSave} from '../../apps/five-axis-cam/webpage/rapidPlanning.mjs';
import {rapidPathCommand,rapidPathFieldChanged,handleRapidPathPick,cancelRapidPathMode,manualRapidPath,renderRapidPathEditor,disposeRapidPathEditing,syncRapidPathEditing,beginRapidHudEdit,finishRapidHudEdit} from '../../apps/five-axis-cam/webpage/rapidPathEditing.mjs';

async function fixture(){
  const ends=[[-40,0,0],[40,0,0]],points=[ends[0],[-10,0,0],[20,0,0],ends[1]],directions=points.map(()=>[0,0,1]);
  let native={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',placements:[],workpieces:[],intentToolpaths:[],setups:[{id:'setup',generatedGeometry:{status:'Current',resource:{url:'resource://rapid',version:'1'},operations:[],transitions:[{from:'a',to:'b',points,directions,frame:'Setup',geometryStatus:'Generated',policy:{strategy:'Quick',marginMm:.2,liftHeightMm:100}},{from:'b',to:'c',points,directions,frame:'Setup',geometryStatus:'Generated',policy:{strategy:'Quick',marginMm:.2,liftHeightMm:100}}]}}]};
  // This suite exercises authored Polyline controls. Automatic curves now keep
  // their construction definition and are covered by the shape-editor suite.
  for(const t of native.setups[0].generatedGeometry.transitions){t.planningMethod='Manual';t.manualPath={points:structuredClone(points.slice(1,-1)),directions:structuredClone(directions.slice(1,-1))};}
  const calls=[],history=[],saves=[];let isCurrent=true,save=async()=>{};
  const context={isCurrentProject:()=>isCurrent,projectProxy:{state:{projectPath:'C:/test/rapid.i5cam'},async autosave(path){saves.push(path);await save();}},sceneProxy:{async invoke(method,payload){
    if(method==='CamScene.ReadState')return structuredClone(native);
    assert.equal(method,'CamRapid.EditPath');assert.deepEqual(payload.expected,native.expected);
    calls.push(structuredClone(payload));history.push(structuredClone(native));const t=native.setups[0].generatedGeometry.transitions[0];
    t.manualPath=structuredClone(payload.manualPath);t.planningMethod='Manual';t.geometryStatus='Draft';t.validation='Unchecked';t.points=[ends[0],...payload.manualPath.points,ends[1]];t.directions=[[0,0,1],...payload.manualPath.directions,[0,0,1]];native.expected.mutationSequence=String(Number(native.expected.mutationSequence)+1);return {};
  }}};
  const view={scene:{domain:structuredClone(native),intentToolpaths:[]},fiveAxisArea:'toolpath'},ops={renderProject(){},appendProjectLog(){}};
  selectTool(view,'rapid');openRapid(view);await rapidPathCommand(context,view,'path-edit',ops);
  view.fiveRapid.saveTopology=()=>saveRapidPathTopology(context,view,ops);
  const camera=new THREE.OrthographicCamera(-50,50,50,-50,.1,1000);camera.position.set(0,0,100);camera.lookAt(0,0,0);camera.updateMatrixWorld();
  const canvas={getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:1000,height:1000})};view.viewport={camera,renderer:{domElement:canvas}};
  const event=(x,button=0)=>({button,clientX:(x+50)*10,clientY:500}),pick=(x,button=0)=>handleRapidPathPick(context,view,null,null,event(x,button),[],ops);
  const command=name=>rapidPathCommand(context,view,`path-${name}`,ops),idle=async()=>{await view.fiveRapid.topologySaving;await view.fiveRapid.savingPath;};
  return {view,context,ops,calls,history,saves,ends,pick,command,idle,setSave(fn){save=fn;},setCurrent(value){isCurrent=value;},get edit(){return view.fiveRapid.pathEdit;}};
}

test('Add/Delete are mutually exclusive persistent modes; exiting a mode does not write history',async()=>{
  const f=await fixture();assert.equal(f.edit.inserting,false);assert.equal(f.edit.deleting,false);
  await f.command('insert');assert.equal(f.edit.inserting,true);assert.equal(f.edit.deleting,false);
  await f.command('delete');assert.equal(f.edit.inserting,false);assert.equal(f.edit.deleting,true);
  await f.command('delete');assert.equal(f.edit.deleting,false);assert.equal(cancelRapidPathMode(f.view),false);
  await f.command('insert');assert.equal(cancelRapidPathMode(f.view),true);assert.equal(f.edit.inserting,false);assert.equal(f.calls.length,0);
  const html=renderRapidPathEditor(f.view,false);assert.match(html,/five-pose-action-grid/);assert.match(html,/path-insert" aria-pressed="false"/);assert.match(html,/path-delete" aria-pressed="false"/);assert.doesNotMatch(html,/取消插入/);
});
test('end editing immediately hides controls while a slow archive save retains the final authored coordinates',async()=>{
 const f=await fixture();let release;f.setSave(()=>new Promise(resolve=>release=resolve));rapidPathFieldChanged(f.view,'pathX','-15');const edit=f.edit;
 f.view.fiveWorkpieceSaveError='previous autosave failed';f.view.error=f.view.fiveWorkpieceSaveError;
 await rapidCommand(f.context,f.view,'path-cancel',f.ops);assert.equal(f.view.fiveRapid.editClosing,true);assert.equal(f.edit,edit);assert.doesNotMatch(renderRapidPathEditor(f.view,false),/结束编辑|path-insert|pathX/);
 while(!release)await new Promise(setImmediate);assert.equal(f.calls.length,1);assert.equal(f.calls[0].manualPath.points[0][0],-15);assert.equal(f.edit,edit);
 release();await f.view.fiveRapid.editClosingTask;assert.equal(f.edit,null);assert.equal(f.history.length,1);assert.equal(f.saves.length,1);assert.equal(f.view.fiveWorkpieceSaveError,'');assert.equal(f.view.error,'');
});
test('ending during a pending native reply does not lose or resend the in-flight path transaction',async()=>{
 const f=await fixture(),invoke=f.context.sceneProxy.invoke;let release;
 f.context.sceneProxy.invoke=async(method,payload)=>{const result=await invoke(method,payload);if(method==='CamRapid.EditPath')await new Promise(resolve=>release=resolve);return result;};
 rapidPathFieldChanged(f.view,'pathX','-16');const saving=flushRapidPathSave(f.context,f.view,f.ops,true);while(!release)await new Promise(setImmediate);
 await rapidCommand(f.context,f.view,'path-cancel',f.ops);assert.equal(f.view.fiveRapid.editClosing,true);assert.doesNotMatch(renderRapidPathEditor(f.view,false),/结束编辑/);
 release();await saving;await f.view.fiveRapid.editClosingTask;assert.equal(f.edit,null);assert.equal(f.calls.length,1);assert.equal(f.calls[0].manualPath.points[0][0],-16);
});
test('a failed end-edit save restores the editor without losing the native change or adding another undo step on retry',async()=>{
 const f=await fixture();f.setSave(async()=>{throw Error('disk unavailable');});rapidPathFieldChanged(f.view,'pathX','-17');const edit=f.edit;
 await rapidCommand(f.context,f.view,'path-cancel',f.ops);await f.view.fiveRapid.editClosingTask;assert.equal(f.edit,edit);assert.match(f.view.error,/disk unavailable/);assert.equal(f.view.fiveRapid.editClosing,false);assert.equal(f.calls.length,1);
 f.setSave(async()=>{});await rapidCommand(f.context,f.view,'path-cancel',f.ops);await f.view.fiveRapid.editClosingTask;assert.equal(f.edit,null);assert.equal(f.calls.length,1);assert.equal(f.history.length,1);
});

test('three rapid Add clicks receive separate native transactions; busy clicks are ignored rather than coalesced',async()=>{
  const f=await fixture();await f.command('insert');let release;f.setSave(()=>new Promise(resolve=>release=resolve));
  f.pick(-25);assert.ok(f.view.fiveRapid.topologySaving);assert.equal(f.edit.points.length,5);
  f.pick(5);f.pick(30);assert.equal(f.edit.points.length,5,'a pending acknowledged point owns the editor');
  while(!release)await new Promise(resolve=>setImmediate(resolve));assert.equal(f.calls.length,1);assert.equal(f.calls[0].manualPath.points.length,3);release();await f.idle();
  f.setSave(async()=>{});f.pick(5);await f.idle();f.pick(30);await f.idle();assert.equal(f.edit.points.length,7);assert.equal(f.edit.inserting,true);assert.deepEqual(f.calls.map(c=>c.manualPath.points.length),[3,4,5]);assert.equal(f.history.length,3);assert.equal(f.saves.length,3);
  assert.deepEqual(f.edit.points[0],f.ends[0]);assert.deepEqual(f.edit.points.at(-1),f.ends[1]);assert.deepEqual(f.edit.directions,Array.from({length:7},()=>[0,0,1]));
});

test('the active mode can exit immediately during native/file saving without cancelling its accepted point',async()=>{
  const f=await fixture();await f.command('insert');let release;f.setSave(()=>new Promise(resolve=>release=resolve));f.pick(-25);
  while(!release)await new Promise(resolve=>setImmediate(resolve));f.view.pending=true;f.view.fiveRapid.request={commandId:'pending'};
  const html=renderRapidPathEditor(f.view,'disabled');assert.doesNotMatch(html.match(/<button[^>]*path-insert[^>]*>/)[0],/disabled/);assert.match(html.match(/<button[^>]*path-delete[^>]*>/)[0],/disabled/);
  await f.command('insert');assert.equal(f.edit.inserting,false);assert.equal(f.edit.deleting,false);assert.ok(f.view.fiveRapid.topologySaving);assert.equal(f.edit.points.length,5);
  f.view.pending=false;f.view.fiveRapid.request=null;release();await f.idle();assert.equal(f.calls.length,1);assert.equal(f.edit.inserting,false);assert.equal(f.edit.points.length,5);
});

test('Delete clicks remove only existing interior points, persist to the last point, and keyboard Delete stays one-shot',async()=>{
  const f=await fixture();await f.command('delete');f.pick(-40);f.pick(40);f.pick(5);assert.equal(f.calls.length,0);assert.equal(f.edit.points.length,4);
  f.pick(-10);await f.idle();assert.equal(f.edit.deleting,true);assert.equal(f.edit.points.length,3);f.pick(20);await f.idle();assert.equal(f.edit.points.length,2);assert.equal(f.edit.deleting,true);assert.deepEqual(f.calls.map(c=>c.manualPath.points.length),[1,0]);
  assert.equal(cancelRapidPathMode(f.view),true);assert.equal(f.edit.deleting,false);
  const other=await fixture();await other.command('delete-point');await other.idle();assert.equal(other.edit.points.length,3);assert.equal(other.edit.deleting,false);assert.equal(other.edit.inserting,false);
});

test('navigation, inactive projects, stale snapshots and invalid coordinates cannot edit rapid topology',async()=>{
  const f=await fixture();await f.command('insert');f.pick(-25,1);f.pick(-25,2);assert.equal(f.edit.points.length,4);
  f.setCurrent(false);f.pick(-25);assert.equal(f.edit.points.length,4);f.setCurrent(true);
  f.view.scene.domain.expected.epoch='another-scene';f.pick(-25);assert.equal(f.edit.points.length,4);f.view.scene.domain.expected.epoch='epoch';
  f.edit.values[2]='-';f.pick(-25);assert.equal(f.edit.points.length,4);assert.throws(()=>manualRapidPath(f.view),/有效的路径点坐标/);assert.equal(f.edit.values[2],'-');assert.equal(f.calls.length,0);
  await f.command('delete');await f.command('delete-point');assert.equal(f.edit.points.length,4);assert.equal(f.edit.values[2],'-');
});

test('failed file persistence blocks further topology edits and retries without a second native transaction',async()=>{
  const f=await fixture();await f.command('insert');f.setSave(async()=>{throw Error('disk unavailable');});f.pick(-25);await f.idle();assert.equal(f.view.fiveRapid.topologyError,true);assert.equal(f.calls.length,1);
  f.pick(5);assert.equal(f.edit.points.length,5);assert.match(renderRapidPathEditor(f.view,false),/重试保存/);
  f.setSave(async()=>{});await f.command('retry');assert.equal(f.view.fiveRapid.topologyError,false);assert.equal(f.calls.length,1);assert.equal(f.saves.length,2);f.pick(5);await f.idle();assert.equal(f.calls.length,2);assert.equal(f.edit.inserting,true);
});

test('page disposal, path switching and reopening the editor clear both modes without changing saved points',async()=>{
  const f=await fixture();await f.command('insert');disposeRapidPathEditing(f.view);assert.equal(f.edit.inserting,false);
  await f.command('delete');rapidPick(f.view,'b/c');assert.equal(f.view.fiveRapid.pathEdit,null);await f.command('edit');assert.equal(f.edit.inserting,false);assert.equal(f.edit.deleting,false);
  await f.command('insert');await f.command('cancel');assert.equal(f.view.fiveRapid.pathEdit,null);await f.command('edit');assert.equal(f.edit.inserting,false);assert.equal(f.edit.deleting,false);assert.equal(f.calls.length,0);
});

test('Add/Delete modes remove the drag gizmo, and leaving the mode restores normal point editing',async()=>{
  const f=await fixture(),vp=f.view.viewport;let added=0,cleared=0;vp.scene=new THREE.Scene();vp.renderer.domElement.addEventListener=()=>{};vp.renderer.domElement.removeEventListener=()=>{};
  vp.setCameraNavigationEnabled=()=>{};vp.setTransformManipulator=()=>{vp.transformManipulator={};added++;};vp.clearTransformManipulator=()=>{vp.transformManipulator=null;cleared++;};
  syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(added,1);await f.command('insert');syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(vp.transformManipulator,null);assert.equal(cleared,1);
  await f.command('delete');syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(vp.transformManipulator,null);cancelRapidPathMode(f.view);syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(added,2);disposeRapidPathEditing(f.view);
});

test('rapid Euler and IJK editing persists normalized intermediate directions through the shared native saver',async()=>{
  const f=await fixture();rapidPathFieldChanged(f.view,'angle-0','30');await flushRapidPathSave(f.context,f.view,f.ops);
  assert.equal(f.calls.length,1);assert.equal(f.saves.length,1);assert.ok(Math.abs(f.calls[0].manualPath.directions[0][1]+.5)<1e-7);
  rapidPathFieldChanged(f.view,'notation','IJK');rapidPathFieldChanged(f.view,'0','1');await flushRapidPathSave(f.context,f.view,f.ops);
  assert.equal(f.calls.length,2);assert.equal(f.history.length,2);assert.equal(f.saves.length,2);assert.ok(Math.abs(Math.hypot(...f.calls[1].manualPath.directions[0])-1)<1e-12);
  assert.deepEqual(f.edit.points[0],f.ends[0]);assert.deepEqual(f.edit.points.at(-1),f.ends[1]);assert.deepEqual(f.edit.directions[0],[0,0,1]);assert.deepEqual(f.edit.directions.at(-1),[0,0,1]);
  assert.match(renderRapidPathEditor(f.view,false),/空移姿态 I/);
});

test('invalid rapid posture remains a draft and blocks native saving, point switching and deletion until corrected',async()=>{
  const f=await fixture();rapidPathFieldChanged(f.view,'angle-0','-');assert.throws(()=>manualRapidPath(f.view),/有效的姿态角/);
  await assert.rejects(flushRapidPathSave(f.context,f.view,f.ops),/有效的姿态角/);assert.equal(f.calls.length,0);
  rapidPathFieldChanged(f.view,'pathPoint','2');assert.equal(f.edit.selected,1);
  await f.command('delete-point');assert.equal(f.edit.points.length,4);
  rapidPathFieldChanged(f.view,'angle-0','15');await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1);
  rapidPathFieldChanged(f.view,'notation','IJK');for(const field of ['0','1','2'])rapidPathFieldChanged(f.view,field,'0');assert.throws(()=>manualRapidPath(f.view),/不能为零向量/);assert.equal(f.calls.length,1);
});

test('rapid endpoint posture is editable and preserved while a different intermediate direction rotates',async()=>{
  const f=await fixture();rapidPathFieldChanged(f.view,'pathPoint','0');rapidPathFieldChanged(f.view,'angle-0','45');assert.equal(f.edit.changed,true);const endpoint=[...f.edit.directions[0]];assert.notDeepEqual(endpoint,[0,0,1]);
  assert.deepEqual(manualRapidPath(f.view).start,{point:f.ends[0],direction:endpoint});
  rapidPathFieldChanged(f.view,'pathPoint','1');await f.command('pose');const vp=f.view.viewport;vp.scene=new THREE.Scene();vp.renderer.domElement.addEventListener=()=>{};vp.renderer.domElement.removeEventListener=()=>{};vp.setCameraNavigationEnabled=()=>{};let options;vp.setTransformManipulator=o=>{options=o;vp.transformManipulator={};};vp.clearTransformManipulator=()=>{vp.transformManipulator=null;};
  syncRapidPathEditing(f.context,f.view,f.ops,()=>flushRapidPathSave(f.context,f.view,f.ops));assert.equal(options.mode,'rotate');options.onStart({axis:'X',direction:[0,0,1]});options.onChange({angle:Math.PI/2});assert.ok(Math.abs(f.edit.directions[1][1]+1)<1e-12);assert.deepEqual(f.edit.points,[f.ends[0],[-10,0,0],[20,0,0],f.ends[1]]);
  options.onEnd({moved:true});await f.idle();assert.equal(f.calls.length,1);assert.deepEqual(f.edit.endpointOverrides.start.direction,endpoint);assert.deepEqual(f.calls[0].manualPath.start.direction,endpoint);assert.deepEqual(f.edit.directions.at(-1),[0,0,1]);disposeRapidPathEditing(f.view);
});

test('invalid pose rejects rotation capture while an outstanding native save allows the next gesture',async()=>{
  const f=await fixture();await f.command('pose');const vp=f.view.viewport;vp.scene=new THREE.Scene();vp.renderer.domElement.addEventListener=()=>{};vp.renderer.domElement.removeEventListener=()=>{};vp.setCameraNavigationEnabled=()=>{};
  let options;vp.setTransformManipulator=o=>{options=o;vp.transformManipulator={};};vp.clearTransformManipulator=()=>{vp.transformManipulator=null;};
  syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(options.enabled,true);
  rapidPathFieldChanged(f.view,'angle-0','-');syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(options.enabled,false,'invalid Euler text must not expose an active rotation ring');
  assert.equal(options.onStart({axis:'X',direction:[0,0,1]}),false);assert.equal(f.edit.gesture,undefined);
  rapidPathFieldChanged(f.view,'angle-0','15');syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(options.enabled,true);
  f.view.fiveRapid.savingPath=Promise.resolve();syncRapidPathEditing(f.context,f.view,f.ops);assert.equal(options.enabled,true,'a prior native receipt must not own the next local gesture');
  options.onStart({axis:'X',direction:[0,0,1]});assert.ok(f.edit.gesture);assert.equal(f.calls.length,0);options.onCancel();
  f.view.fiveRapid.savingPath=null;disposeRapidPathEditing(f.view);
});

test('a delayed native acknowledgement never commits an active gesture preview as another history step',async()=>{
  const f=await fixture(),proxy=f.context.sceneProxy,original=proxy.invoke.bind(proxy);let release,held=false;
  proxy.invoke=async(method,payload)=>{const result=await original(method,payload);if(method==='CamRapid.EditPath'&&!held){held=true;await new Promise(resolve=>release=resolve);}return result;};
  rapidPathFieldChanged(f.view,'angle-0','30');const saving=flushRapidPathSave(f.context,f.view,f.ops,true);
  while(!release)await new Promise(resolve=>setImmediate(resolve));assert.equal(f.calls.length,1);
  // A second local gesture present at the receipt boundary stays a preview.
  f.edit.gesture={before:{points:structuredClone(f.edit.points),directions:structuredClone(f.edit.directions),selected:f.edit.selected}};
  f.edit.directions[f.edit.selected]=[1,0,0];f.edit.changed=true;f.edit.revision++;
  release();await saving;assert.equal(f.calls.length,1,'the acknowledgement cannot drain a live gesture into a second native edit');assert.equal(f.history.length,1);assert.equal(f.saves.length,1);assert.equal(f.edit.changed,true);
  delete f.edit.gesture;await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,2,'only the released final gesture can create its own native edit');assert.equal(f.history.length,2);assert.deepEqual(f.calls[1].manualPath.directions[0],[1,0,0]);assert.deepEqual(f.edit.points[0],f.ends[0]);assert.deepEqual(f.edit.points.at(-1),f.ends[1]);
});

test('rotation cancellation and overlay disposal restore the clean automatic-route draft without native history',async()=>{
  const f=await fixture();await f.command('pose');const vp=f.view.viewport;vp.scene=new THREE.Scene();vp.renderer.domElement.addEventListener=()=>{};vp.renderer.domElement.removeEventListener=()=>{};vp.setCameraNavigationEnabled=()=>{};let options;
  vp.setTransformManipulator=o=>{options=o;vp.transformManipulator={};};vp.clearTransformManipulator=()=>{vp.transformManipulator=null;};
  const before={points:structuredClone(f.edit.points),directions:structuredClone(f.edit.directions),changed:f.edit.changed,revision:f.edit.revision};
  syncRapidPathEditing(f.context,f.view,f.ops,()=>flushRapidPathSave(f.context,f.view,f.ops));options.onStart({axis:'X',direction:[0,0,1]});options.onChange({angle:Math.PI/4});assert.equal(f.edit.changed,true);
  options.onCancel();await f.idle();assert.deepEqual({points:f.edit.points,directions:f.edit.directions,changed:f.edit.changed,revision:f.edit.revision},before);assert.equal(f.edit.gesture,undefined);assert.equal(f.calls.length,0);assert.equal(f.history.length,0);
  syncRapidPathEditing(f.context,f.view,f.ops);options.onStart({axis:'Y',direction:[0,0,1]});options.onChange({angle:Math.PI/3});disposeRapidPathEditing(f.view);await f.idle();assert.deepEqual({points:f.edit.points,directions:f.edit.directions,changed:f.edit.changed,revision:f.edit.revision},before);assert.equal(f.edit.gesture,undefined);assert.equal(f.calls.length,0);assert.equal(f.saves.length,0);
});

test('floating rapid input waits for confirmation; Escape restores its baseline while sidebar input keeps automatic saving',async()=>{
 const f=await fixture();f.view.fiveRapid.savePath=()=>scheduleRapidPathSave(f.context,f.view,f.ops,20);const baseline=structuredClone(f.edit.points);
 assert.equal(beginRapidHudEdit(f.view),true);rapidPathFieldChanged(f.view,'pathX','-15');await new Promise(resolve=>setTimeout(resolve,40));assert.equal(f.calls.length,0);assert.equal(f.view.fiveRapid.saveTimer,null);
 await assert.rejects(flushRapidPathSave(f.context,f.view,f.ops),/确认或取消浮动输入/);
 assert.equal(finishRapidHudEdit(f.view,true),true);assert.deepEqual(f.edit.points,baseline);assert.equal(f.edit.changed,false);assert.equal(f.calls.length,0);
 beginRapidHudEdit(f.view);rapidPathFieldChanged(f.view,'angle-0','-');assert.equal(finishRapidHudEdit(f.view,true),true);assert.doesNotThrow(()=>manualRapidPath(f.view));assert.equal(f.calls.length,0);
 beginRapidHudEdit(f.view);rapidPathFieldChanged(f.view,'pathX','-15');finishRapidHudEdit(f.view,false);await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1);assert.equal(f.history.length,1);assert.equal(f.saves.length,1);
 rapidPathFieldChanged(f.view,'pathX','-12');await new Promise(resolve=>setTimeout(resolve,40));await f.idle();assert.equal(f.calls.length,2,'ordinary sidebar changes retain their original automatic saver');assert.equal(f.edit.points[1][0],-12);
});
