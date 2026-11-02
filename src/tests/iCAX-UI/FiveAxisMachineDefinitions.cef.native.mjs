import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
try{
 const reopen=process.argv.includes('--reopen'),browser=await chromium.connectOverCDP('http://127.0.0.1:9482'),page=browser.contexts()[0].pages()[0],out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',saved=out+'/builtin-definitions.i5cam',path=reopen?saved:out+'/工业机床碰撞验证.i5cam';
 await page.setViewportSize({width:1600,height:1000});await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),path);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&mv?.scene?.machineDefinitions&&!mv.pending;},path);
 await page.locator('[data-tab-id="simulation"]').click();await page.locator('[data-command-id="five.tool.machine-definitions"]').click();
 await page.waitForFunction(()=>mv?.scene?.machineDefinitions&&!mv.pending);
 const defs=await page.evaluate(()=>mv.scene.machineDefinitions),gantry='b21ac27d-75e8-4847-a80a-4c25feaa3015',robot='bf0d5260-2b94-4454-929e-c075b1ee1600';
 const properties=page.locator('[data-five-key="definition-properties"]'),rows=page.locator('[role="list"][aria-label="机床定义"] [role="listitem"]');
 assert.equal(await page.locator('[data-command-id*="sample"],[data-five-command*="sample"]').count(),0);
 assert.equal(await page.locator('[data-five-key="machine-definitions"] [data-five-command^="five.machine.instantiate:"]').count(),0);
 if(reopen){
  assert.equal(defs.length,3);assert.equal(defs.find(d=>d.id===robot).enabled,false);assert.ok(!defs.some(d=>d.id===gantry));assert.equal(await rows.count(),3);
  const instances=await page.evaluate(()=>mv.scene.machines.filter(m=>m.name.includes('IR-1600')||m.name.includes('LC-3015')).length);assert.ok(instances>=3);
  writeFileSync(out+'/builtin-reopen-acceptance.json',JSON.stringify({pass:true,definitions:defs,instances,noReseeding:true},null,2));console.log(JSON.stringify({pass:true,reopen:true,deletedDefinitionStaysDeleted:true,disabledDefinitionStaysDisabled:true,instancesRetained:true}));process.exit(0);
 }
 assert.deepEqual(new Set(defs.map(d=>d.id)),new Set([gantry,robot,'f4ed0130-37ef-45fb-9883-7af0b1fc1000','a30b2cab-c634-4b3a-94c0-c7ad1eac0800']));assert.equal(await rows.count(),4);
 const baseline=await page.evaluate(()=>mv.scene.machines.length);
 for(const id of [gantry,robot]){
  await page.locator(`[data-five-command="five.machine.definition-select:${id}"]`).click();assert.ok((await properties.textContent()).includes(id===gantry?'URDF':'SDF'));
  await page.locator('[data-five-key="machine-definition-manager"] [data-five-command^="five.machine.instantiate:"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
  assert.equal(await page.evaluate(()=>mv.fiveMachine.error),'');await page.locator('[data-command-id="five.tool.machine-definitions"]').click();
 }
 assert.equal(await page.evaluate(()=>mv.scene.machines.length),baseline+2);
 await page.evaluate(()=>mv.viewport.fitViewToViewport?.(1));await page.screenshot({path:out+'/builtin-definition-list.png'});
 assert.equal(await properties.locator('[role="switch"],input[type="checkbox"]').count(),0);
 await page.locator(`[data-five-command="five.machine.definition-select:${gantry}"]`).click();
 await page.evaluate(()=>{window.definitionUi={rows:[...document.querySelectorAll('.five-machine-definition-row')],properties:document.querySelector('[data-five-key="definition-properties"]'),canvas:mv.viewport.renderer.domElement};});
 const status=page.locator(`[data-five-command="five.machine.definition-toggle:${robot}"]`);
 await status.click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
 assert.equal(await status.getAttribute('aria-checked'),'false');assert.equal(await status.textContent(),'已禁用');
 assert.equal(await page.locator(`[data-five-command="five.machine.definition-select:${gantry}"]`).getAttribute('aria-pressed'),'true');assert.ok((await properties.textContent()).includes('LC-3015'));
 assert.deepEqual(await page.evaluate(()=>({rows:definitionUi.rows.every((e,i)=>e===document.querySelectorAll('.five-machine-definition-row')[i]),properties:definitionUi.properties===document.querySelector('[data-five-key="definition-properties"]'),canvas:definitionUi.canvas===mv.viewport.renderer.domElement})),{rows:true,properties:true,canvas:true});
 await status.press('Space');await page.waitForFunction(()=>!mv.fiveMachine.busy);assert.equal(await status.getAttribute('aria-checked'),'true');
 await status.click();await page.waitForFunction(()=>!mv.fiveMachine.busy);
 await page.locator(`[data-five-command="five.machine.definition-select:${robot}"]`).click();
 assert.equal(await page.locator('[data-five-key="machine-definition-manager"] [data-five-command^="five.machine.instantiate:"]').isDisabled(),true);
 const rejection=await page.evaluate(async id=>{try{await mv.viewportSceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:id});return '';}catch(e){return e.message;}},robot);assert.match(rejection,/disabled/);
 await page.locator(`[data-five-command="five.machine.definition-select:${gantry}"]`).click();await page.locator('[data-five-key="machine-definition-manager"] [data-five-command^="five.machine.definition-delete:"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);assert.equal(await rows.count(),3);
 // Exercise the regular Import command; only the OS file picker is supplied by the test.
 const source='D:/penguinchinage0609/iCAX/src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf';
 await page.evaluate(source=>{const bridge=globalThis.icax??globalThis.icaxNativeBridge;window.restoreMachinePicker=()=>bridge.openFileDialog=window.machineOriginalPicker;window.machineOriginalPicker=bridge.openFileDialog;bridge.openFileDialog=async()=>source;},source);
 try{await page.locator('[data-five-command="five.machine.import"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);}finally{await page.evaluate(()=>restoreMachinePicker());}
 const imported=await page.evaluate(ids=>mv.scene.machineDefinitions.find(d=>!ids.includes(d.id)),defs.map(d=>d.id));assert.ok(imported);assert.notEqual(imported.id,gantry);
 await page.locator(`[data-five-command="five.machine.definition-select:${imported.id}"]`).click();await page.locator('[data-five-key="machine-definition-manager"] [data-five-command^="five.machine.definition-delete:"]').click();await page.waitForFunction(()=>!mv.fiveMachine.busy);assert.equal(await rows.count(),3);
 await page.evaluate(path=>mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path}),saved);
 writeFileSync(out+'/builtin-prepare-acceptance.json',JSON.stringify({pass:true,defaults:defs,createdInstances:2,disabledRejected:rejection,importedId:imported.id,deletedId:gantry},null,2));console.log(JSON.stringify({pass:true,freshDefaults:4,listOnly:true,createdInstances:2,ordinaryImportDelete:true,disabledCannotInstantiate:true}));process.exit(0);
}catch(e){console.error(e);process.exit(1);}
