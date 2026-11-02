import assert from 'node:assert/strict';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';

export async function runRapidContinuousDragCase({page,read,idle,command,output}){
 mkdirSync(output,{recursive:true});const report={passed:false,stages:[],errors:[]},geometry=s=>s.setups.find(x=>x.id===s.activeSetupId).generatedGeometry;
 const field=name=>page.locator(`[data-five-rapid-field="${name}"]`).first(),button=name=>page.locator(`[data-five-command="five.rapid.${name}"]`).first();
 const before=await read(),corner=geometry(before).transitions.find(t=>t.planningMethod==='CornerBridge');assert.ok(corner);
 await command('five.tool.rapid');await field('connection').selectOption(`${corner.from}/${corner.to}`);await idle();await button('path-edit').click();await idle();
 assert.equal(await page.evaluate(()=>fixture.view.fiveRapid.pathEdit.pathKind),'Polyline');
 assert.equal(await page.evaluate(()=>fixture.view.fiveRapid.pathEdit.points.length),5);
 const editing=()=>page.evaluate(()=>{const e=fixture.view.fiveRapid.pathEdit;return {points:structuredClone(e.points),directions:structuredClone(e.directions),selected:e.selected};});
 const definition=e=>({points:e.points.slice(1,-1),directions:e.directions.slice(1,-1)});
 const baseDriver=gizmoDriver(page),driver={async drag(axis,pixels,release=true){
  const point=await baseDriver.locate(axis),before=await page.evaluate(p=>{const v=fixture.view,vp=v.viewport,m=vp.transformManipulator,n=document.elementFromPoint(p.x,p.y),r=vp.renderer.domElement.getBoundingClientRect();return {axis:m.control.axis,pointer:m.pointerId,dragging:m.control.dragging,proxy:m.proxy.position.toArray(),target:n?.className,rect:{x:r.x,y:r.y,width:r.width,height:r.height}};},point);
  await page.mouse.move(point.x,point.y);
  const after=await page.evaluate(p=>{const m=fixture.view.viewport.transformManipulator,n=document.elementFromPoint(p.x,p.y);return {axis:m.control.axis,pointer:m.pointerId,dragging:m.control.dragging,proxy:m.proxy.position.toArray(),target:n?.className};},point);
  const event={axis,point,before,after,stage:report.stages.length};report.axisEvents??=[];report.axisEvents.push(event);writeFileSync(resolve(output,'axis-events.json'),JSON.stringify(report.axisEvents,null,2));
  assert.equal(after.axis,axis,JSON.stringify(event));await page.mouse.down();await page.mouse.move(point.x+point.dx*pixels,point.y+point.dy*pixels,{steps:8});if(release)await page.mouse.up();return point;
 }};let previous=structuredClone(corner.manualPath);
 for(const stage of ['native','read','history','file']){
  console.log('continuous drag during '+stage);
  const initial=await editing(),camera=await page.evaluate(()=>fixture.view.viewport.getCameraState()),ends=await page.evaluate(()=>{const p=fixture.view.fiveRapid.pathEdit.points;return [p[0],p.at(-1)];});
  await page.evaluate(stage=>{
   const v=fixture.view,{context}=v.fiveRapid.saveBinding,proxy=context.sceneProxy,actions=context.actions,project=context.projectProxy;
   const original={invoke:proxy.invoke.bind(proxy),history:actions.refreshNativeProjectHistory?.bind(actions),autosave:project.autosave.bind(project)};
   const d=window.rapidDragDelay={stage,proxy,actions,project,original,calls:[],held:false,used:false};
   const gate=async target=>{if(d.stage!==target||d.used)return;d.used=true;d.held=true;await new Promise(resolve=>d.release=()=>{d.held=false;resolve();});};
   proxy.invoke=async(method,...args)=>{if(method==='CamRapid.EditPath')d.calls.push(structuredClone(args[0]));const result=await original.invoke(method,...args);if(method==='CamRapid.EditPath')await gate('native');if(method==='CamScene.ReadState')await gate('read');return result;};
   actions.refreshNativeProjectHistory=async(...args)=>{const result=await original.history?.(...args);await gate('history');return result;};
   project.autosave=async(...args)=>{const result=await original.autosave(...args);await gate('file');return result;};
  },stage);
  try{
   await driver.drag('X',12);await page.waitForFunction(()=>rapidDragDelay.held);
   assert.equal(await page.evaluate(()=>fixture.view.viewport.transformManipulator.control.enabled),true);
   await driver.drag('Y',10);
   if(stage==='file')await page.waitForFunction(()=>rapidDragDelay.calls.length>=2);
   await driver.drag('Z',8,false);
   const held=await page.evaluate(()=>{const v=fixture.view,e=v.fiveRapid.pathEdit,m=v.viewport.transformManipulator;return {path:{points:structuredClone(e.points),directions:structuredClone(e.directions),selected:e.selected},gesture:!!e.gesture,axis:m.control.axis,pointer:m.pointerId,revision:e.revision};});
   assert.equal(held.gesture,true);assert.equal(held.axis,'Z');assert.ok(held.pointer!=null);assert.notDeepEqual(held.path.points[held.path.selected],initial.points[initial.selected]);
   await page.evaluate(()=>rapidDragDelay.release());await page.waitForFunction(()=>!fixture.view.fiveRapid.savingPath);
   const ack=await page.evaluate(()=>{const e=fixture.view.fiveRapid.pathEdit,m=fixture.view.viewport.transformManipulator;return {path:{points:e.points,directions:e.directions,selected:e.selected},gesture:!!e.gesture,axis:m.control.axis,pointer:m.pointerId,revision:e.revision};});
   assert.deepEqual(ack,held,'an older acknowledgement must preserve the entire live next-axis gesture');
   await page.mouse.up();await idle();const final=await editing(),saved=geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`);
   assert.deepEqual(final,held.path);assert.deepEqual(saved.manualPath,definition(final));assert.deepEqual(final.directions,initial.directions);for(let i=0;i<final.points.length;i++)if(i!==final.selected)assert.deepEqual(final.points[i],initial.points[i]);
   for(let i=0;i<3;i++)assert.ok(Math.abs(final.points[final.selected][i]-initial.points[initial.selected][i])>1e-5,`${stage}: axis ${i} displacement was lost`);
   assert.deepEqual(await page.evaluate(()=>fixture.view.viewport.getCameraState()),camera);assert.deepEqual(await page.evaluate(()=>{const p=fixture.view.fiveRapid.pathEdit.points;return [p[0],p.at(-1)];}),ends);
   const calls=await page.evaluate(()=>rapidDragDelay.calls);report.stages.push({stage,realDrags:['X','Y','Z'],initial,final,nativeTransactions:calls.length,committedSnapshots:calls.map(c=>c.manualPath)});writeFileSync(resolve(output,'continuous-drag-progress.json'),JSON.stringify(report,null,2));
   assert.ok(calls.length>=2,'the initial X transaction and a later completed draft both reach native history');previous=calls.length>1?calls.at(-2).manualPath:previous;
  }finally{
   await page.mouse.up();await page.evaluate(()=>{const d=window.rapidDragDelay;d.release?.();d.proxy.invoke=d.original.invoke;d.actions.refreshNativeProjectHistory=d.original.history;d.project.autosave=d.original.autosave;});
  }
 }
 const final=await read(),latest=geometry(final).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`).manualPath;
 await page.getByRole('button',{name:'撤销',exact:true}).click();await idle();const undone=geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`).manualPath;assert.deepEqual(undone,previous);
 await page.getByRole('button',{name:'重做',exact:true}).click();await idle();const redone=geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`).manualPath;assert.deepEqual(redone,latest);
 report.history={undoRestoresPreviousCommittedSnapshot:true,redoRestoresFinalSnapshot:true,latest,previous};
 await driver.drag('Y',6);await idle();const afterHistory=geometry(await read()).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`).manualPath;
 assert.notDeepEqual(afterHistory,latest,'Undo/Redo completion must allow an immediate next-axis drag');report.afterHistoryGesture={axis:'Y',capturedAndPersisted:true,before:latest,after:afterHistory};
 const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths','layers'])assert.deepEqual(after[key],before[key],key+' changed');assert.deepEqual(geometry(after).operations,geometry(before).operations);
 report.finalTransition=geometry(after).transitions.find(t=>`${t.from}/${t.to}`===`${corner.from}/${corner.to}`);report.passed=true;
 await page.screenshot({path:resolve(output,'continuous-drag.png')});writeFileSync(resolve(output,'continuous-drag-report.json'),JSON.stringify(report,null,2));return report;
}
