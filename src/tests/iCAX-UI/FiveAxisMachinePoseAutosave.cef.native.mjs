import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,writeFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',project=out+'/pose-autosave.i5cam';
try{
 copyFileSync(out+'/ik-adjusted.i5cam',project);
 const b=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=b.contexts()[0].pages()[0];
 await page.setViewportSize({width:1600,height:900});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machines?.length&&!mv.pending;});
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 const machineId=await page.evaluate(async()=>{const d=mv.scene.machineDefinitions.find(d=>d.name.includes('IR-1600')),r=await mv.viewportSceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:d.id});return r.machine.entityId;});
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('five.refresh'));await page.waitForFunction(()=>!mv.pending);
 await page.locator('[data-five-machine-instance]').selectOption(machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 await page.evaluate(async()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),j=m.joints.find(j=>j.jointName==='J5');await mv.viewportSceneProxy.invoke('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId:m.entityId,positions:{[j.entityId]:.3}});await __icaxAppShell.executeRibbonCommand('five.refresh');});
 await page.waitForFunction(()=>!mv.pending);
 const tip=await page.evaluate(()=>mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints.find(j=>j.jointName==='J6').entityId);
 await page.evaluate(id=>__icaxAppShell.executeRibbonCommand('five.machine.part:'+id),tip);
 const command=mode=>page.evaluate(mode=>__icaxAppShell.executeRibbonCommand('five.machine-ik.'+mode),mode);
 const read=()=>page.evaluate(()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),o=mv.viewport.sceneObjects.get(mv.fiveMachineSelectedPartId);o.updateWorldMatrix(true,false);return {joints:Object.fromEntries(m.joints.filter(j=>j.type!=='fixed').map(j=>[j.entityId,j.position])),world:o.matrixWorld.elements.slice(),result:mv.fiveMachineIK.lastResult};});
 const settle=()=>page.waitForFunction(()=>{const s=mv.fiveMachineIK;return !mv.fiveHistoryBusy&&!s.editTask&&!s.inflight&&!s.finishing&&!s.fileSaving&&![...(s.drafts?.values()||[])].some(p=>p.timer||p.requested);},null,{timeout:60000});
 assert.equal(await page.locator('[data-five-command="five.machine-ik.apply"]').count(),0);
 const x=page.locator('[data-five-machine-field="ik-pose:0"]'),rz=page.locator('[data-five-machine-field="ik-pose:5"]'),before=await read();
 await x.fill(String(before.world[12]+.5));await x.evaluate(e=>{window.poseInput=e;e.setSelectionRange(1,4);window.poseCanvas=mv.viewport.renderer.domElement;});await settle();
 const moved=await read();assert.equal(moved.result.status,'Ready');assert.ok(Math.abs(moved.world[12]-before.world[12]-.5)<.01);assert.ok(Object.keys(before.joints).filter(id=>Math.abs(before.joints[id]-moved.joints[id])>1e-7).length>=2);
 assert.deepEqual(await x.evaluate(e=>({node:e===poseInput,focus:e===document.activeElement,range:[e.selectionStart,e.selectionEnd],canvas:poseCanvas===mv.viewport.renderer.domElement})),{node:true,focus:true,range:[1,4],canvas:true});
 await x.blur();await page.locator('[data-action="undo-project"]').click();await settle();assert.deepEqual((await read()).joints,before.joints);
 await page.locator('[data-action="redo-project"]').click();await settle();assert.deepEqual((await read()).joints,moved.joints);
 await rz.fill(String(Number(await rz.inputValue())+.05));await settle();const rotated=await read();assert.equal(rotated.result.status,'Ready');assert.notDeepEqual(rotated.joints,moved.joints);await rz.blur();
 // Delayed native reply while the user continues typing and scrolling.
 await page.evaluate(()=>{const proxy=mv.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);window.holdPose=true;proxy.invoke=async(method,...args)=>{const result=await invoke(method,...args);if(method==='CamSimulation.MoveMachineIK'&&window.holdPose)await new Promise(resolve=>window.releasePose=resolve);return result;};});
 await x.fill(String(rotated.world[12]+.2));await page.waitForFunction(()=>!!window.releasePose);
 const finalX=String(rotated.world[12]+.4);await x.fill(finalX);await x.evaluate(e=>{e.setSelectionRange(1,3);window.panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')];panes.forEach(p=>p.scrollTop+=20);window.scrolls=panes.map(p=>p.scrollTop);});
 await page.evaluate(()=>{holdPose=false;releasePose();});await settle();assert.ok(Math.abs((await read()).world[12]-Number(finalX))<.01);
 assert.deepEqual(await x.evaluate(e=>({value:e.value,node:e===poseInput,focus:e===document.activeElement,range:[e.selectionStart,e.selectionEnd],scroll:panes.every((p,i)=>p.scrollTop===scrolls[i])})),{value:finalX,node:true,focus:true,range:[1,3],scroll:true});await x.blur();
 const valid=await read();await x.fill('1000000');await settle();assert.equal((await read()).result.status,'Unreachable');assert.deepEqual((await read()).joints,valid.joints);await x.blur();await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.deepEqual((await read()).joints,valid.joints);
 await command('translate');await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
 const beforeDrag=await read();await gizmoDriver(page).drag('X',5);await settle();const afterDrag=await read();assert.equal(afterDrag.result.status,'Ready');assert.notDeepEqual(afterDrag.joints,beforeDrag.joints);
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.deepEqual((await read()).joints,beforeDrag.joints);await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.redo'));await settle();assert.deepEqual((await read()).joints,afterDrag.joints);
 const joints=[];for(const id of Object.keys(afterDrag.joints)){const r=await page.evaluate(id=>mv.viewportSceneProxy.invoke('Machine.GetElement',{entityId:id}),id);joints.push([id,r.machineElement.joint]);}
 writeFileSync(out+'/pose-autosave-expected.json',JSON.stringify({project,machineId,tip,joints},null,2));await page.screenshot({path:out+'/pose-autosave.png'});
 writeFileSync(out+'/pose-autosave-acceptance.json',JSON.stringify({pass:true,positionAndRotation:true,parentJoints:true,topbarUndoRedo:true,delayedReplyFocusScroll:true,unreachableHeld:true,dragUndoRedo:true},null,2));
 console.log('PASS pose autosave, exact-joint undo/redo, parent IK, input retention, rejection and gesture history');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
