import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {createConstructionEdit} from '../../apps/five-axis-cam/webpage/rapidConstructionEditing.mjs';
import {openRapid,flushRapidPathSave} from '../../apps/five-axis-cam/webpage/rapidPlanning.mjs';
import {syncPathSelectionStroke} from '../../apps/five-axis-cam/webpage/pathSelectionStroke.mjs';
import {rapidDependencyKey} from '../../apps/five-axis-cam/webpage/rapidDependencies.mjs';
import {rapidPathCommand,rapidPathFieldChanged,handleRapidPathPick,manualRapidPath,manualRapidDraft,renderRapidPathEditor,syncRapidPathEditing,refreshRapidPathEdit,beginRapidHudEdit,finishRapidHudEdit} from '../../apps/five-axis-cam/webpage/rapidPathEditing.mjs';

function source(kind='Arch',count=129){
 const construction={kind,followHeightMm:1,liftHeightMm:10,lateralOffset:[0,2,0]},sourceStart={point:[-40,0,0],direction:[0,0,1]},sourceEnd={point:[40,0,0],direction:[0,0,1]};
 const points=Array.from({length:count},(_,i)=>{const t=i/(count-1);return [-40+80*t*t*(3-2*t),32*t*t*(1-t)*(1-t),kind==='Follow'?1:1+40*t*(1-t)];});
 return {construction,sourceStart,sourceEnd,shapeHandle:{point:[0,2,kind==='Follow'?1:11],direction:[0,0,1]},points,directions:points.map(()=>[0,0,1]),from:'a',to:'b',fromPlacementId:'p',frame:'Setup',geometryStatus:'Generated',planningMethod:'Direct',policy:{strategy:kind==='Follow'?'Fly':'Frog',marginMm:.2,liftHeightMm:10}};
}
async function fixture({kind='Arch',polyline=false,count=129,missingConstruction=false,turnPoint}={}){
 const result=source(kind,count);
 if(turnPoint){result.construction={...result.construction,turnPoint:[...turnPoint],lateralOffset:[0,0,0]};result.sourceEnd.direction=[0,1,0];result.shapeHandle={point:[...turnPoint],direction:[0,1,0]};result.points=[[-40,0,1],[-40,0,11],[...turnPoint],[40,11,0],[40,1,0]];result.directions=[[0,0,1],[0,0,1],[0,1,0],[0,1,0],[0,1,0]];}
 const t=structuredClone(result);if(missingConstruction){delete t.construction;delete t.shapeHandle;delete t.sourceStart;delete t.sourceEnd;}
 if(polyline){delete t.construction;t.manualPath={points:structuredClone(result.points.slice(1,-1)),directions:structuredClone(result.directions.slice(1,-1))};t.planningMethod='Manual';}
 const state={expected:{epoch:'shape',mutationSequence:'1'},activeSetupId:'s',placements:[],workpieces:[],intentToolpaths:[],setups:[{id:'s',rapidSettings:{followHeightMm:1},generatedGeometry:{status:'Current',revision:{id:'g'},operations:[],transitions:[t]}}]},calls=[];
 const view={scene:{domain:structuredClone(state),intentToolpaths:[]},fiveAxisArea:'toolpath'},ops={renderProject(){},appendProjectLog(){}};
 const context={projectProxy:{state:{projectPath:'C:/shape.i5cam'},async autosave(){}},isCurrentProject:()=>true,sceneProxy:{async invoke(method,payload){
  if(method==='CamScene.ReadState')return structuredClone(state);
  calls.push({method,payload:structuredClone(payload)});
  if(method==='CamRapid.ReadEditPath')return structuredClone(result);
  assert.equal(method,'CamRapid.EditPath');const saved=state.setups[0].generatedGeometry.transitions[0];saved.manualPath=structuredClone(payload.manualPath);saved.geometryStatus='Draft';saved.planningMethod='Manual';saved.validation='Unchecked';
  if(payload.manualPath.construction){saved.construction=structuredClone(payload.manualPath.construction);saved.shapeHandle=structuredClone(view.fiveRapid.pathEdit.points[1]);saved.shapeHandle={point:saved.shapeHandle,direction:[0,0,1]};saved.sourceStart=result.sourceStart;saved.sourceEnd=result.sourceEnd;saved.points=structuredClone(view.fiveRapid.pathEdit.samplePoints);saved.directions=structuredClone(view.fiveRapid.pathEdit.sampleDirections);}
  else{saved.points=[result.points[0],...payload.manualPath.points,result.points.at(-1)];saved.directions=[result.directions[0],...payload.manualPath.directions,result.directions.at(-1)];}
  state.expected.mutationSequence=String(Number(state.expected.mutationSequence)+1);return {};
 }}};
 selectTool(view,'rapid');openRapid(view);await rapidPathCommand(context,view,'path-edit',ops);
 const camera=new THREE.OrthographicCamera(-50,50,50,-50,.1,1000);camera.position.set(0,0,100);camera.lookAt(0,0,0);camera.updateMatrixWorld();
 const listeners={};const canvas={getBoundingClientRect:()=>({x:0,y:0,left:0,top:0,width:1000,height:1000}),addEventListener(name,handler){listeners[name]=handler;},removeEventListener(name){delete listeners[name];}};
 let manipulator;
 view.viewport={camera,scene:new THREE.Scene(),renderer:{domElement:canvas},setCameraNavigationEnabled(){},setTransformManipulator(value){manipulator=value;this.transformManipulator={};},clearTransformManipulator(){this.transformManipulator=null;}};
 return {view,context,ops,calls,state,result,get edit(){return view.fiveRapid.pathEdit;},get manipulator(){return manipulator;},sync(){syncRapidPathEditing(context,view,ops,()=>{});},command(name){return rapidPathCommand(context,view,'path-'+name,ops);},pointerDown(index){const p=new THREE.Vector3(...view.fiveRapid.pathEdit.points[index]).project(camera);listeners.pointerdown({button:0,buttons:1,clientX:(p.x+1)*500,clientY:(1-p.y)*500});},pick(index,shiftKey=false){const p=new THREE.Vector3(...view.fiveRapid.pathEdit.points[index]).project(camera);return handleRapidPathPick(context,view,null,null,{button:0,clientX:(p.x+1)*500,clientY:(1-p.y)*500,shiftKey},[],ops);}};
}
test('scene command sequence changes do not invalidate unchanged manual geometry or its editor',async()=>{
 const f=await fixture({polyline:true,count:5});const key=rapidDependencyKey(f.view),edit=f.edit;
 f.state.expected.mutationSequence='9';f.view.scene.domain.expected.mutationSequence='9';
 assert.equal(rapidDependencyKey(f.view),key);f.sync();assert.ok(f.view.fiveRapidPathOverlay);assert.equal(f.edit,edit);
 f.edit.selected=1;f.edit.selectedPoints=[1];f.edit.values=f.edit.points[1].map(String);f.view.fiveRapid.pathEdit.values[2]=String(f.edit.points[1][2]+1);
 rapidPathFieldChanged(f.view,'pathZ',String(f.edit.points[1][2]+1));await flushRapidPathSave(f.context,f.view,f.ops);
 assert.equal(f.calls.at(-1).payload.expected.mutationSequence,'9');
 f.view.scene.domain.setups[0].generatedGeometry.transitions[0].inputKey='actual-geometry-changed';assert.notEqual(rapidDependencyKey(f.view),key);
});

test('dense automatic curves keep their formula handle without exposing samples or writing a Polyline',async()=>{
 const f=await fixture({missingConstruction:true});assert.equal(f.calls.length,1);assert.equal(f.calls[0].method,'CamRapid.ReadEditPath');
 assert.equal(f.edit.points.length,3);assert.equal(f.edit.samplePoints.length,129);assert.deepEqual(f.edit.points[1],f.result.shapeHandle.point);
 assert.deepEqual(manualRapidPath(f.view),{construction:f.result.construction});assert.deepEqual(manualRapidDraft(f.view).points,f.result.points);
 const html=renderRapidPathEditor(f.view,false);assert.equal((html.match(/<option /g)||[]).length,3);assert.doesNotMatch(html,/path-insert|path-delete|中间点 127/);
 assert.equal(f.calls.filter(c=>c.method==='CamRapid.EditPath').length,0);
});
test('one Arch handle changes lift and guide while both physical endpoints remain exact',async()=>{
 const f=await fixture(),ends=[structuredClone(f.edit.points[0]),structuredClone(f.edit.points.at(-1))];f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[0,4,6]});f.manipulator.onEnd({moved:true});
 assert.equal(f.edit.construction.liftHeightMm,16);assert.deepEqual(f.edit.construction.lateralOffset,[0,6,0]);assert.deepEqual([f.edit.points[0],f.edit.points.at(-1)],ends);
 assert.deepEqual([f.edit.samplePoints[0],f.edit.samplePoints.at(-1)],ends);assert.equal(f.edit.construction.followHeightMm,1);
 await flushRapidPathSave(f.context,f.view,f.ops);const request=f.calls.find(c=>c.method==='CamRapid.EditPath').payload;
 assert.deepEqual(Object.keys(request.manualPath),['construction']);assert.equal(f.edit.points.length,3);assert.equal(f.edit.changed,false);
});
test('Follow uses only a lateral guide and never changes the process height or endpoints',async()=>{
 const f=await fixture({kind:'Follow'}),ends=[structuredClone(f.edit.points[0]),structuredClone(f.edit.points.at(-1))];f.sync();assert.deepEqual(f.manipulator.axes,['X','Y']);assert.equal(f.manipulator.space,'local');assert.equal(f.manipulator.freeTranslate,false);
 f.manipulator.onStart();f.manipulator.onChange({translation:[0,4,6]});f.manipulator.onEnd({moved:true});
 assert.deepEqual(f.edit.points[1],[0,6,1]);assert.equal(f.edit.construction.followHeightMm,1);assert.equal(f.edit.construction.liftHeightMm,10);assert.deepEqual([f.edit.points[0],f.edit.points.at(-1)],ends);
 assert.doesNotMatch(renderRapidPathEditor(f.view,false),/pathLiftHeight|path-pose/);await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.at(-1).payload.manualPath.construction.followHeightMm,1);
});
test('Follow endpoint preview retains edited TCP and direction when its shape handle is adjusted later',async()=>{
 const f=await fixture({kind:'Follow'}),end=[...f.edit.points.at(-1)];rapidPathFieldChanged(f.view,'pathPoint','0');rapidPathFieldChanged(f.view,'pathZ','4');rapidPathFieldChanged(f.view,'angle-0','10');
 const start=[...f.edit.points[0]],axis=[...f.edit.directions[0]];assert.deepEqual(f.edit.samplePoints[0],start);assert.deepEqual(f.edit.sampleDirections[0],axis);assert.deepEqual(f.edit.samplePoints.at(-1),end);
 rapidPathFieldChanged(f.view,'pathPoint','1');f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[0,2,0]});f.manipulator.onEnd({moved:true});
 assert.deepEqual(f.edit.samplePoints[0],start);assert.deepEqual(f.edit.sampleDirections[0],axis);assert.deepEqual(manualRapidPath(f.view).start,{point:start,direction:axis});assert.equal(manualRapidPath(f.view).construction.kind,'Follow');
});
test('saved dense Polyline stays intact and a whole interior group moves in one native transaction',async()=>{
 const f=await fixture({polyline:true}),before=structuredClone(f.edit.points);assert.equal(f.edit.points.length,129);assert.deepEqual(f.edit.points,f.result.points);assert.equal(f.calls.length,0);
 await f.command('select-all');assert.equal(f.edit.selectedPoints.length,127);f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[3,4,5]});f.manipulator.onEnd({moved:true});
 assert.deepEqual(f.edit.points[0],before[0]);assert.deepEqual(f.edit.points.at(-1),before.at(-1));for(let i=1;i<128;i++)assert.deepEqual(f.edit.points[i],before[i].map((n,k)=>n+[3,4,5][k]));
 await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1);assert.equal(f.calls[0].payload.manualPath.points.length,127);assert.equal(f.calls[0].payload.manualPath.construction,undefined);
});
test('Shift selects a control group and numeric movement updates the group without moving either endpoint',async()=>{
 const f=await fixture({polyline:true,count:5}),before=structuredClone(f.edit.points);f.pick(2,true);assert.deepEqual(f.edit.selectedPoints,[1,2]);
 const delta=3;rapidPathFieldChanged(f.view,'pathX',String(f.edit.points[f.edit.selected][0]+delta));
 assert.equal(f.edit.points[1][0],before[1][0]+delta);assert.equal(f.edit.points[2][0],before[2][0]+delta);assert.deepEqual(f.edit.points[3],before[3]);assert.deepEqual(f.edit.points[0],before[0]);assert.deepEqual(f.edit.points.at(-1),before.at(-1));
 await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1);
});
test('invalid curve height cannot save; endpoint XYZ and IJK remain independent authored controls',async()=>{
 const f=await fixture(),ends=[structuredClone(f.edit.points[0]),structuredClone(f.edit.points.at(-1))];rapidPathFieldChanged(f.view,'pathLiftHeight','-');assert.equal(f.edit.liftInput,'-');assert.throws(()=>manualRapidPath(f.view),/曲线高度/);
 await assert.rejects(flushRapidPathSave(f.context,f.view,f.ops),/曲线高度/);assert.equal(f.calls.length,0);rapidPathFieldChanged(f.view,'pathLiftHeight','14');
 rapidPathFieldChanged(f.view,'pathPoint','0');rapidPathFieldChanged(f.view,'pathX','100');rapidPathFieldChanged(f.view,'angle-0','90');assert.deepEqual(f.edit.points[0],[100,0,1]);assert.deepEqual(f.edit.points.at(-1),ends[1]);assert.notDeepEqual(f.edit.directions[0],[0,0,1]);
 const manual=manualRapidPath(f.view);assert.deepEqual(manual.start,{point:f.edit.points[0],direction:f.edit.directions[0]});assert.equal(manual.end,undefined);assert.equal(manual.construction.kind,'Arch');
 rapidPathFieldChanged(f.view,'0','-');assert.throws(()=>manualRapidPath(f.view),/有效的 IJK/);rapidPathFieldChanged(f.view,'pathPoint','2');assert.equal(f.edit.selected,0);
});
test('authoritative readback keeps control metadata separate from the dense rendered path',async()=>{
 const f=await fixture(),source={...structuredClone(f.result),points:Array.from({length:211},(_,i)=>[i-105,0,1]),directions:Array.from({length:211},()=>[0,0,1]),shapeHandle:{point:[2,3,12],direction:[0,0,1]}};
 f.edit.revision=9;refreshRapidPathEdit(f.view,source);assert.equal(f.edit.revision,9);assert.equal(f.edit.points.length,3);assert.equal(f.edit.samplePoints.length,211);assert.deepEqual(f.edit.points[1],[2,3,12]);assert.deepEqual(manualRapidPath(f.view),{construction:f.result.construction});
});
test('a failed collision preview keeps the authored curve visible and cannot claim clearance',async()=>{
 const f=await fixture();f.view.fiveRapid.preview={result:{geometryStatus:'Failed',validation:'Collision',points:[],directions:[]}};
 const draft=manualRapidDraft(f.view);assert.deepEqual(draft.points,f.edit.samplePoints);assert.ok(draft.points.length>3);assert.equal(draft.geometryStatus,'Failed');assert.equal(draft.validation,'Collision');
});
test('oversized height text cannot save the previous value and a later gesture updates the displayed height',async()=>{
 const f=await fixture();rapidPathFieldChanged(f.view,'pathLiftHeight','1000001');assert.throws(()=>manualRapidPath(f.view),/曲线高度/);assert.equal(f.calls.length,0);
 rapidPathFieldChanged(f.view,'pathLiftHeight','14');f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[0,0,6]});f.manipulator.onEnd({moved:true});
 assert.equal(f.edit.construction.liftHeightMm,20);assert.equal(f.edit.liftInput,undefined);assert.match(renderRapidPathEditor(f.view,false),/aria-label="曲线高度" value="20"/);
});
test('a Retract exposes its original five vertices and saves edits as the same Polyline with locked endpoints',async()=>{
 const f=await fixture({kind:'Retract',turnPoint:[5,7,20]}),initial=structuredClone(f.result.points);
 assert.deepEqual(f.edit.points,initial);assert.deepEqual(manualRapidPath(f.view).points,initial.slice(1,-1));assert.equal(f.calls.length,0);assert.match(renderRapidPathEditor(f.view,false),/<strong>折线控制点<\/strong>/);
 assert.match(renderRapidPathEditor(f.view,false),/path-insert/);assert.match(renderRapidPathEditor(f.view,false),/path-delete/);
 f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[4,-3,2]});f.manipulator.onEnd({moved:true});
 assert.deepEqual(f.edit.points,[initial[0],initial[1],[9,4,22],initial[3],initial[4]]);assert.deepEqual(f.edit.directions[2],f.result.sourceEnd.direction);
 rapidPathFieldChanged(f.view,'pathY','16');assert.deepEqual(f.edit.points[2],[9,16,22]);await flushRapidPathSave(f.context,f.view,f.ops);
 const payload=f.calls.find(c=>c.method==='CamRapid.EditPath').payload.manualPath;assert.deepEqual(payload,{points:f.edit.points.slice(1,-1),directions:f.edit.directions.slice(1,-1)});assert.equal(payload.construction,undefined);
 assert.deepEqual(f.edit.points[0],initial[0]);assert.deepEqual(f.edit.points.at(-1),initial.at(-1));
});
test('an explicit turn is restricted to Retract and cannot be combined with a lateral guide',async()=>{
 const arch=await fixture();arch.edit.construction.turnPoint=[1,2,3];assert.throws(()=>manualRapidPath(arch.view),/转向位置/);assert.equal(arch.calls.length,0);
 const retract=await fixture({kind:'Retract',turnPoint:[5,7,20]});retract.view.fiveRapid.pathEdit=createConstructionEdit(retract.result);retract.edit.values=[...retract.edit.points[1]];retract.edit.construction.lateralOffset=[0,1,0];retract.edit.changed=true;
 await assert.rejects(flushRapidPathSave(retract.context,retract.view,retract.ops),/转向位置/);assert.equal(retract.calls.length,0);
 retract.edit.construction.lateralOffset=[0,0,0];assert.deepEqual(manualRapidPath(retract.view).construction.turnPoint,[5,7,20]);
});
test('a transform handle owns pointerdown even when its screen position overlaps an editable endpoint',async()=>{
 const f=await fixture();f.sync();f.view.viewport.transformManipulator.hover=event=>{assert.equal(event.buttons,0);return true;};
 f.pointerDown(2);assert.equal(f.edit.selected,1);assert.deepEqual(f.edit.selectedPoints,[1]);assert.equal(f.edit.selectionDown,undefined);assert.equal(f.calls.length,0);
 f.view.viewport.transformManipulator.hover=()=>false;f.pointerDown(2);assert.equal(f.edit.selected,2);assert.deepEqual(f.edit.selectedPoints,[2]);
});
const until=async predicate=>{for(let i=0;i<100&&!predicate();i++)await new Promise(resolve=>setImmediate(resolve));assert.ok(predicate());};
test('a delayed X receipt cannot interrupt or overwrite a following live Y gesture',async()=>{
 const f=await fixture({kind:'Retract',turnPoint:[5,7,20]}),proxy=f.context.sceneProxy,invoke=proxy.invoke.bind(proxy);let release;
 proxy.invoke=async(method,payload)=>{const result=await invoke(method,payload);if(method==='CamRapid.EditPath'&&!release)await new Promise(resolve=>release=resolve);return result;};
 f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[3,0,0]});f.manipulator.onEnd({moved:true});const first=flushRapidPathSave(f.context,f.view,f.ops,true);await until(()=>!!release);
 f.sync();assert.equal(f.manipulator.enabled,true);f.manipulator.onStart();f.manipulator.onChange({translation:[0,4,0]});const latest=structuredClone(f.edit.points);
 release();await first;assert.ok(f.edit.gesture);assert.deepEqual(f.edit.points,latest);assert.equal(f.calls.length,1);assert.equal(f.edit.changed,true);
 f.manipulator.onEnd({moved:true});await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,2);assert.deepEqual(f.calls.at(-1).payload.manualPath.points[1],[8,11,20]);assert.equal(f.edit.changed,false);f.sync();assert.equal(f.manipulator.enabled,true);
});
test('an acknowledgement between next-axis down and move permits Escape without repeating the saved transaction',async()=>{
 const f=await fixture({kind:'Retract',turnPoint:[5,7,20]}),proxy=f.context.sceneProxy,invoke=proxy.invoke.bind(proxy);let release;
 proxy.invoke=async(method,payload)=>{const result=await invoke(method,payload);if(method==='CamRapid.EditPath'&&!release)await new Promise(resolve=>release=resolve);return result;};
 f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[3,0,0]});f.manipulator.onEnd({moved:true});const first=flushRapidPathSave(f.context,f.view,f.ops,true);await until(()=>!!release);
 f.sync();f.manipulator.onStart();release();await first;assert.ok(f.edit.gesture);f.manipulator.onCancel();
 assert.deepEqual(f.edit.points[2],[8,7,20]);assert.equal(f.edit.changed,false);assert.equal(f.view.fiveRapid.dirty,false);await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1);
});
test('a no-move next-axis release or unchanged HUD confirmation settles the acknowledged revision without another transaction',async()=>{
 for(const mode of ['release','hud']){
  const f=await fixture({kind:'Retract',turnPoint:[5,7,20]}),proxy=f.context.sceneProxy,invoke=proxy.invoke.bind(proxy);let release;
  proxy.invoke=async(method,payload)=>{const result=await invoke(method,payload);if(method==='CamRapid.EditPath'&&!release)await new Promise(resolve=>release=resolve);return result;};
  f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[3,0,0]});f.manipulator.onEnd({moved:true});const first=flushRapidPathSave(f.context,f.view,f.ops,true);await until(()=>!!release);f.sync();
  if(mode==='release')f.manipulator.onStart();else assert.equal(beginRapidHudEdit(f.view),true);
  release();await first;if(mode==='release')f.manipulator.onEnd({moved:false});else finishRapidHudEdit(f.view,false);
  assert.equal(f.edit.changed,false,mode);assert.equal(f.view.fiveRapid.dirty,false,mode);await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1,mode);
 }
});
test('a pending archive save does not serialize following Y and Z native edits behind disk I/O',async()=>{
 const f=await fixture({kind:'Retract',turnPoint:[5,7,20]});let release,saves=0;
 f.context.projectProxy.autosave=async()=>{if(++saves===1)await new Promise(resolve=>release=resolve);return {saved:true};};
 f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[3,0,0]});f.manipulator.onEnd({moved:true});const first=flushRapidPathSave(f.context,f.view,f.ops,true);await until(()=>!!release);assert.equal(f.view.fiveRapid.savingPath,null);
 f.sync();assert.equal(f.manipulator.enabled,true);f.manipulator.onStart();f.manipulator.onChange({translation:[0,4,0]});f.manipulator.onEnd({moved:true});const second=flushRapidPathSave(f.context,f.view,f.ops,true);await until(()=>f.calls.length===2&&!f.view.fiveRapid.savingPath);
 f.sync();f.manipulator.onStart();f.manipulator.onChange({translation:[0,0,2]});f.manipulator.onEnd({moved:true});const third=flushRapidPathSave(f.context,f.view,f.ops,true);await until(()=>f.calls.length===3&&!f.view.fiveRapid.savingPath);
 assert.deepEqual(f.calls.at(-1).payload.manualPath.points[1],[8,11,22]);release();await Promise.all([first,second,third]);assert.equal(f.edit.changed,false);assert.ok(saves<=2,'archive writes are coalesced by the existing shared writer');
});
test('a failed coordinate archive save retries the existing native transaction rather than writing it again',async()=>{
 const f=await fixture({kind:'Retract',turnPoint:[5,7,20]});let writes=0;
 f.context.projectProxy.autosave=async()=>{if(++writes===1)throw Error('disk unavailable');return {saved:true};};
 rapidPathFieldChanged(f.view,'pathX','8');await assert.rejects(flushRapidPathSave(f.context,f.view,f.ops),/disk unavailable/);
 assert.equal(f.calls.length,1);assert.equal(f.edit.changed,false);assert.equal(f.view.fiveRapid.needsFileSave,true);
 await flushRapidPathSave(f.context,f.view,f.ops);assert.equal(f.calls.length,1);assert.equal(writes,2);assert.equal(f.view.fiveRapid.needsFileSave,false);
});
test('an explicit save awaiting disk refuses to complete across a newly started gesture and native history prevents capture',async()=>{
 const f=await fixture({kind:'Retract',turnPoint:[5,7,20]});let release;
 f.context.projectProxy.autosave=async()=>{await new Promise(resolve=>release=resolve);return {saved:true};};
 rapidPathFieldChanged(f.view,'pathX','8');const saving=flushRapidPathSave(f.context,f.view,f.ops);await until(()=>!!release);
 f.sync();f.manipulator.onStart();release();await assert.rejects(saving,/结束空移点拖动/);f.manipulator.onEnd({moved:false});
 f.view.fiveHistoryBusy=true;f.sync();assert.equal(f.manipulator.enabled,true);assert.equal(f.manipulator.onStart(),false);assert.equal(f.edit.gesture,undefined);assert.equal(beginRapidHudEdit(f.view),false);
 f.view.fiveHistoryBusy=false;f.manipulator.onStart();assert.ok(f.edit.gesture,'history completion does not need an extra render to restore capture');f.manipulator.onCancel();
});
test('pointer motion patches only the chosen dense rapid, its thick stroke and controls while retaining all cut nodes',async()=>{
 const f=await fixture(),group=new THREE.Group(),other=new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(),new THREE.Vector3(1,2,3)]),new THREE.LineBasicMaterial());
 const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints(f.edit.samplePoints.map(p=>new THREE.Vector3(...p))),new THREE.LineDashedMaterial());line.computeLineDistances();line.userData.rapidTransitionId=f.view.fiveRapid.id;group.add(other,line);syncPathSelectionStroke(line,true);f.view.fiveGeneratedPreview={group,key:'before'};
 const oldLineGeometry=line.geometry,stroke=line.children[0],oldStrokeGeometry=stroke.geometry;let disposed=0,renders=0;oldLineGeometry.addEventListener('dispose',()=>disposed++);oldStrokeGeometry.addEventListener('dispose',()=>disposed++);f.ops.renderProject=()=>renders++;
 f.sync();f.manipulator.onStart();assert.equal(renders,1);f.manipulator.onChange({translation:[3,4,5]});
 assert.equal(renders,1);assert.equal(group.children[0],other);assert.equal(group.children[1],line);assert.equal(line.children[0],stroke);assert.equal(disposed,2,'resampling disposes only the replaced target GPU geometries');
 const positions=line.geometry.getAttribute('position');assert.equal(positions.count,f.edit.samplePoints.length);assert.ok(positions.count>3);assert.equal(stroke.geometry.instanceCount,positions.count-1);
 for(let i=0;i<positions.count;i++)assert.deepEqual([positions.getX(i),positions.getY(i),positions.getZ(i)],f.edit.samplePoints[i].map(Math.fround));
 for(let i=1;i<positions.count;i++){const a=stroke.geometry.getAttribute('segmentStart'),b=stroke.geometry.getAttribute('segmentEnd');assert.deepEqual([a.getX(i-1),a.getY(i-1),a.getZ(i-1)],f.edit.samplePoints[i-1].map(Math.fround));assert.deepEqual([b.getX(i-1),b.getY(i-1),b.getZ(i-1)],f.edit.samplePoints[i].map(Math.fround));}
 const geometry=line.geometry,distances=geometry.getAttribute('lineDistance'),strokeGeometry=stroke.geometry;f.manipulator.onChange({translation:[4,5,5]});assert.equal(line.geometry,geometry);assert.equal(geometry.getAttribute('lineDistance'),distances);assert.equal(stroke.geometry,strokeGeometry);assert.equal(renders,1);assert.equal(f.view.fiveGeneratedPreview.key,null);
 let growingDisposed=0;strokeGeometry.addEventListener('dispose',()=>growingDisposed++);const oldCount=strokeGeometry.instanceCount;f.manipulator.onChange({translation:[4,5,200]});assert.ok(stroke.geometry.instanceCount>oldCount);assert.notEqual(stroke.geometry,strokeGeometry);assert.equal(growingDisposed,1);assert.equal(renders,1);
 f.manipulator.onCancel();assert.equal(renders,2,'Escape performs one complete calibration render');
});
