import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,writeFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',project=out+'/native-history-'+Date.now()+'.i5cam';
try{

 const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0],errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 // Keep the isolated browser focused while the user works in another desktop window.
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(projectPath=>__icaxAppShell.createProject({productId:'icax.five-axis-cam',projectName:'原生撤销验收',projectPath}),project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return !!mv?.scene&&!mv.pending;});
 const cmd=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);
 const settled=()=>page.waitForFunction(()=>{const s=mv.fiveMachineIK,c=mv.fiveMachineColliders;return !!mv.scene&&!mv.pending&&!mv.fiveHistoryBusy&&!mv.fiveMachineJog?.running&&!mv.fiveMachine?.busy&&!s?.inflight&&!s?.editTask&&!s?.finishing&&!s?.fileSaving&&!c?.saving&&![...(s?.drafts?.values()||[])].some(d=>d.timer||d.requested)&&![...(c?.drafts.values()||[])].some(d=>d.timer||d.requested||d.dirty);},null,{timeout:60000});
 const history=()=>page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState());
 const undo=async()=>{await page.locator('[data-action="undo-project"]').click();await settled();};
 const redo=async()=>{await page.locator('[data-action="redo-project"]').click();await settled();};
 await page.locator('[data-tab-id="simulation"]').click();await cmd('five.tool.machine-instances');await settled();
 const definition=await page.evaluate(()=>mv.scene.machineDefinitions.find(d=>d.name.includes('IR-1600')).id);
 await cmd('five.machine.instantiate:'+definition);await settled();
 const machineId=await page.evaluate(()=>mv.fiveMachine.machineId);
 const joints=await page.evaluate(()=>mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId).joints);const tip=joints.find(j=>j.jointName==='J6').entityId,j1=joints.find(j=>j.jointName==='J1').entityId,j5=joints.find(j=>j.jointName==='J5').entityId;
 // Move away from the wrist singularity through the same native checked command.
 await page.evaluate(async({machineId,j5})=>{await mv.viewportSceneProxy.invoke('CamSimulation.MoveMachine',{expected:mv.scene.domain.expected,machineId,positions:{[j5]:.3}});await __icaxAppShell.executeRibbonCommand('five.refresh');},{machineId,j5});await settled();
 const read=()=>page.evaluate(async({machineId,tip})=>{const all=await mv.viewportSceneProxy.invoke('Machine.List',{});const m=all.machines.find(m=>m.entityId===machineId);const c=(await mv.viewportSceneProxy.invoke('Machine.GetElement',{entityId:tip})).machineElement.collisions;return {enabled:m.enabled,post:m.postprocessor,joints:m.joints.map(j=>({id:j.entityId,position:j.position,lower:j.lower,upper:j.upper})),boxes:c.map(({id,...c})=>c)};},{machineId,tip});
 const states=[await read()],steps=[],record=async name=>{await settled();steps.push({name,history:await history()});states.push(await read());assert.notDeepEqual(states.at(-1),states.at(-2),name);console.log('STEP',name);};
 await cmd('five.machine.part:'+tip);
 const box=await page.evaluate(()=>mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes[0]);
 let field=page.locator(`[data-five-machine-field="box:${box.id}:size:0"]`);
 await field.fill(String(Number(box.size[0])+.5));await field.evaluate(e=>{window.editedInput=e;window.canvas=mv.viewport.renderer.domElement;e.setSelectionRange(0,2);window.panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')];panes.forEach(p=>p.scrollTop=35);window.scrolls=panes.map(p=>p.scrollTop);});await record('collider size');
 assert.deepEqual(await field.evaluate(e=>({node:e===editedInput,focus:e===document.activeElement,range:[e.selectionStart,e.selectionEnd],canvas:canvas===mv.viewport.renderer.domElement,scroll:panes.every((p,i)=>p.scrollTop===scrolls[i])})),{node:true,focus:true,range:[0,2],canvas:true,scroll:true});await field.blur();
 await page.locator(`[data-five-machine-field="box:${box.id}:enabled"]`).uncheck();await record('collider enable');
 await cmd('five.machine.box-add');await record('collider add');
 const added=await page.evaluate(()=>mv.fiveMachineColliders.active);await cmd('five.machine.box-remove:'+added);await record('collider delete');
 await cmd('five.machine.part:'+j1);await cmd('five.machine.jog-plus:'+j1);await record('jog');
 await page.locator(`[data-five-machine-field="upper:${j1}"]`).fill('179');await cmd('five.machine.limits:'+j1);await record('joint limits');
 await page.locator(`[data-five-machine-field="axis:${j1}:offset"]`).fill('2');await cmd('five.machine.axis-save:'+j1);await record('axis mapping');
 await cmd('five.machine.part:'+machineId);await cmd('five.machine.instance-toggle');await record('instance enable');
 for(let i=states.length-2;i>=0;i--){await undo();assert.deepEqual(await read(),states[i],'undo '+steps[i].name);assert.equal(await page.evaluate(()=>mv.fiveMachine.machineId),machineId);console.log('UNDO',steps[i].name);}
 for(let i=1;i<states.length;i++){await redo();assert.deepEqual(await read(),states[i],'redo '+steps[i-1].name);console.log('REDO',steps[i-1].name);}
 await undo(); // Enabled for IK.
 await cmd('five.machine.part:'+tip);
 const pose=()=>page.evaluate(()=>{const m=mv.scene.machines.find(m=>m.entityId===mv.fiveMachine.machineId),o=mv.viewport.sceneObjects.get(mv.fiveMachineSelectedPartId);o.updateWorldMatrix(true,false);return {joints:m.joints.map(j=>j.position),world:o.matrixWorld.elements.slice()};});
 let before=await pose(),h0=await history();
 const x=page.locator('[data-five-machine-field="ik-pose:0"]');await x.fill(String(before.world[12]+.5));await settled();await x.blur();let after=await pose();assert.notDeepEqual(after.joints,before.joints);assert.equal((await history()).undoSteps.length,h0.undoSteps.length+1);await undo();assert.deepEqual((await pose()).joints,before.joints);await redo();assert.deepEqual((await pose()).joints,after.joints);
 await cmd('five.machine-ik.translate');await page.evaluate(()=>{mv.viewport.setViewDirection([1,-1.5,.8]);mv.viewport.fitViewToViewport(1);});
 before=await pose();h0=await history();await gizmoDriver(page).drag('X',5);await settled();after=await pose();assert.notDeepEqual(after.joints,before.joints);assert.equal((await history()).undoSteps.length,h0.undoSteps.length+1,'entire drag is one repository command');await undo();assert.deepEqual((await pose()).joints,before.joints);await redo();assert.deepEqual((await pose()).joints,after.joints);
 before=await pose();await gizmoDriver(page).drag('X',2,false);await page.waitForFunction(()=>!mv.fiveMachineIK.inflight&&!mv.fiveMachineIK.pending);await page.keyboard.press('Escape');await page.mouse.up();await settled();assert.deepEqual((await pose()).joints,before.joints,'Escape uses repository to restore entire gesture');
 await cmd('five.machine-ik.close');await cmd('five.machine.part:'+j1);before=await pose();h0=await history();
 const hold=page.locator(`[data-five-command="five.machine.jog-plus:${j1}"]`);await hold.scrollIntoViewIfNeeded();const r=await hold.boundingBox();await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();await page.waitForTimeout(650);await page.mouse.up();await settled();after=await pose();assert.notDeepEqual(after.joints,before.joints);assert.equal((await history()).undoSteps.length,h0.undoSteps.length+1,'held jog is one repository command');assert.equal((await history()).undoSteps[0].name,'连续点动');await undo();assert.deepEqual((await pose()).joints,before.joints);await redo();assert.deepEqual((await pose()).joints,after.joints);
 assert.deepEqual(errors,[]);await page.screenshot({path:out+'/native-history.png'});
 writeFileSync(out+'/native-history-report.json',JSON.stringify({pass:true,project,machineId,tip,mixedOperations:steps,ikNumeric:true,ikDragOneStep:true,escapeCancel:true,heldJogOneStep:true,focusSelectionScroll:true,states,final:await read()},null,2));console.log('PASS native repository history');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
