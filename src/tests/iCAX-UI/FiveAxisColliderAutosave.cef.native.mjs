import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copyFileSync,readFileSync,writeFileSync} from 'node:fs';
import {gizmoDriver} from './FiveAxisPlacementGizmo.native.browser.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',path=out+'/collider-autosave.i5cam';
const saved=JSON.parse(readFileSync(out+'/collider-components-acceptance.json','utf8'));
try{
 copyFileSync(out+'/collider-components.i5cam',path);
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
 await page.setViewportSize({width:1600,height:1000});
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async()=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};return mv?.scene?.machines?.length&&!mv.pending;});
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
 await page.locator('[data-five-machine-instance]').selectOption(saved.machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 const command=action=>page.evaluate(action=>__icaxAppShell.executeRibbonCommand('five.machine.'+action),action);
 await command('part:'+saved.tip);
 const boxes=()=>page.evaluate(()=>structuredClone(mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).boxes));
 const baseline=await boxes(),id=baseline[0].id;
 const field=page.locator(`[data-five-machine-field="box:${id}:position:0"]`);
 const settle=()=>page.waitForFunction(()=>{const s=mv.fiveMachineColliders;return !s.saving&&![...s.drafts.values()].some(d=>d.dirty||d.timer||d.requested)&&!s.fileDirty;});
 const native=()=>page.evaluate(id=>mv.viewportSceneProxy.invoke('Machine.GetElement',{entityId:id}),saved.tip);
 const collisions=result=>result.machineElement.collisions;
 assert.equal(await page.locator('[data-five-command="five.machine.box-save"]').count(),0);
 await page.evaluate(()=>{window.calls=[];const proxy=mv.viewportSceneProxy,invoke=proxy.invoke.bind(proxy);proxy.invoke=async(method,args,...rest)=>{if(method==='Machine.SetBoxColliders'){calls.push(structuredClone(args));if(window.holdSave)await new Promise(resolve=>window.releaseSave=resolve);if(window.failSave){window.failSave=false;throw Error('test save failure');}}return invoke(method,args,...rest);};});
 await field.click();await field.fill('81.125');await field.evaluate(e=>{e.setSelectionRange(1,4);window.keptInput=e;window.keptCanvas=mv.viewport.renderer.domElement;});
 await settle();assert.ok(Math.abs(collisions(await native())[0].pose[0]-.081125)<1e-9);
 assert.deepEqual(await field.evaluate(e=>({node:e===keptInput,focus:e===document.activeElement,range:[e.selectionStart,e.selectionEnd],canvas:keptCanvas===mv.viewport.renderer.domElement})),{node:true,focus:true,range:[1,4],canvas:true});
 await field.blur();await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.undo'));await settle();assert.ok(Math.abs(collisions(await native())[0].pose[0]-Number(baseline[0].position[0])/1000)<1e-9);
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('edit.redo'));await settle();assert.ok(Math.abs(collisions(await native())[0].pose[0]-.081125)<1e-9);
 await page.locator(`[data-five-machine-field="box:${id}:enabled"]`).uncheck();await settle();assert.equal(collisions(await native())[0].enabled,false);
 await command('box-add');await settle();assert.equal(collisions(await native()).length,baseline.length+1);
 const added=(await boxes()).at(-1).id;await command('box-remove:'+added);await settle();assert.equal(collisions(await native()).length,baseline.length);
 // Invalid intermediate input never reaches native; fixing it resumes saving.
 await field.click();const count=await page.evaluate(()=>calls.length);await field.fill('-');await page.waitForTimeout(400);assert.equal(await page.evaluate(()=>calls.length),count);
 await field.fill('82');await settle();await field.blur();
 // A delayed old reply cannot replace later typing, focus or current scroll.
 await page.evaluate(()=>window.holdSave=true);await field.fill('83');await page.waitForFunction(()=>!!window.releaseSave);
 await field.fill('84');await field.evaluate(e=>{e.setSelectionRange(0,1);document.querySelector('.cam-info-pane').scrollTop+=35;window.keptScroll=document.querySelector('.cam-info-pane').scrollTop;});
 await page.evaluate(()=>{window.holdSave=false;releaseSave();window.releaseSave=null;});await settle();
 assert.ok(Math.abs(collisions(await native())[0].pose[0]-.084)<1e-9);
 assert.deepEqual(await field.evaluate(e=>({value:e.value,focus:e===document.activeElement,range:[e.selectionStart,e.selectionEnd],scroll:document.querySelector('.cam-info-pane').scrollTop===keptScroll})),{value:'84',focus:true,range:[0,1],scroll:true});await field.blur();
 // Failed writes retain the edit and can be retried without an Apply workflow.
 await page.evaluate(()=>window.failSave=true);await field.fill('85');await page.waitForFunction(()=>!!mv.fiveMachineColliders.drafts.get(mv.fiveMachineColliders.partId).error);
 assert.equal(Number((await boxes())[0].position[0]),85);await command('box-retry');await settle();await field.blur();
 // Drag moves only the local preview. One native write at pointer release.
 await command('box-edit:'+id+':translate');await command('box-focus');
 const beforeDrag=await page.evaluate(()=>calls.length);const driver=gizmoDriver(page);await driver.drag('X',30,false);
 assert.equal(await page.evaluate(()=>calls.length),beforeDrag);await page.mouse.up();await settle();assert.equal(await page.evaluate(()=>calls.length),beforeDrag+1);
 const final=collisions(await native());writeFileSync(out+'/collider-autosave-expected.json',JSON.stringify({path,tip:saved.tip,collisions:final},null,2));
 await page.screenshot({path:out+'/collider-autosave.png'});
 writeFileSync(out+'/collider-autosave-acceptance.json',JSON.stringify({pass:true,checks:['automatic field commit and file save','undo redo persisted','enable add delete persisted','invalid input rejected','delayed replies preserve latest edits and focus/selection/scroll','failed write retained and retried','no native requests during drag; one on release'],calls:await page.evaluate(()=>calls.length)},null,2));
 console.log('Collider autosave native acceptance passed');process.exit(0);
}catch(e){console.error(e);process.exit(1);}
