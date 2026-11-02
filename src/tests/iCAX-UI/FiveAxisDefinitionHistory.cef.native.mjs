import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mkdirSync,writeFileSync,readFileSync,cpSync,renameSync,existsSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root='D:/penguinchinage0609/iCAX',out=root+'/output/tests/machine-simulation',fixture=out+'/definition-input-'+Date.now();
mkdirSync(fixture,{recursive:true});
writeFileSync(fixture+'/box.urdf','<robot name="History box"><link name="base"><visual><geometry><box size="0.2 0.3 0.4"/></geometry></visual><collision><geometry><box size="0.2 0.3 0.4"/></geometry></collision></link></robot>');
cpSync(root+'/src/apps/five-axis-cam/resources/machines/industrial-arm',fixture+'/arm',{recursive:true});
try{
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0],errors=[];
 await page.reload();
 page.on('pageerror',e=>errors.push(e.message));await page.setViewportSize({width:1600,height:1000});
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
 const project=out+'/definition-history-'+Date.now()+'.i5cam';
 await page.evaluate(projectPath=>__icaxAppShell.createProject({productId:'icax.five-axis-cam',projectName:'定义撤销重做验收',projectPath}),project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.product=mv.viewportSceneProxy.project.product;return !!mv.scene&&!mv.pending;});
 const cmd=id=>page.evaluate(id=>__icaxAppShell.executeRibbonCommand(id),id);
 const settle=async()=>{await page.waitForFunction(async()=>{const {definitionHistoryState}=await import('../../../apps/five-axis-cam/webpage/machineDefinitionHistory.mjs');return !__icaxAppShell.getState().pendingCount&&!mv.pending&&!mv.fiveHistoryBusy&&!mv.fiveDefinitionRefresh&&!mv.fiveMachine?.busy&&!definitionHistoryState().blocked;});await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));};
 const list=()=>page.evaluate(async()=>(await product.invoke('MachineDefinition.List',{})).definitions);
 const history=()=>page.evaluate(()=>product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'}));
 const sceneHistory=()=>page.evaluate(()=>mv.viewportSceneProxy.getUndoRedoState());
 const historySignature=()=>page.evaluate(async()=>{const {activeTool}=await import('../../../apps/five-axis-cam/webpage/workspaceModel.mjs');const h=activeTool(mv)==='machine-definitions'?await product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'}):await mv.viewportSceneProxy.getUndoRedoState();return JSON.stringify([h.undoSteps,h.redoSteps]);});
 const waitHistory=async before=>{await page.waitForFunction(async before=>{const {activeTool}=await import('../../../apps/five-axis-cam/webpage/workspaceModel.mjs');const h=activeTool(mv)==='machine-definitions'?await product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'}):await mv.viewportSceneProxy.getUndoRedoState();return JSON.stringify([h.undoSteps,h.redoSteps])!==before;},before);await settle();};
 const undo=async(keyboard=false)=>{const before=await historySignature();await page.waitForFunction(()=>!document.querySelector('[data-action="undo-project"]').disabled);if(keyboard)await page.keyboard.press('Control+z');else await page.locator('[data-action="undo-project"]').click();await waitHistory(before);};
 const redo=async()=>{const before=await historySignature();await page.locator('[data-action="redo-project"]').click();await waitHistory(before);};
 await page.locator('[data-tab-id="simulation"]').click();
 await settle();
 await cmd('five.tool.machine-instances');await settle();
 await page.evaluate(async()=>{while((await product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'})).canUndo)await product.invoke('Product.UndoSettings',{scope:'machineDefinitions'});const {refreshDefinitionHistory}=await import('../../../apps/five-axis-cam/webpage/machineDefinitionHistory.mjs');await refreshDefinitionHistory({productProxy:product});});
 const initial=await list();
 await page.evaluate(id=>mv.viewportSceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:id}),initial.find(d=>d.enabled).id);
 await cmd('five.refresh');await settle();
 const sceneBefore=await sceneHistory(),machinesBefore=await page.evaluate(()=>mv.scene.machines.map(m=>m.entityId));
 assert.equal(sceneBefore.canUndo,true);
 await cmd('five.tool.machine-definitions');await settle();
 assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),true,'empty library history must not undo project');
 const states=[initial],names=[],record=async name=>{await settle();names.push(name);states.push(await list());assert.notDeepEqual(states.at(-1),states.at(-2),name);console.log('STEP',name);};
 const importFile=async path=>{
   await page.evaluate(path=>{window.originalChooser=product.bridge.openFileDialog;product.bridge.openFileDialog=async()=>path;},path);
   try{await page.locator('[data-command-id="five.machine-panel.import"]').click();await settle();assert.equal(await page.evaluate(()=>mv.fiveMachine.error),'');}
   finally{await page.evaluate(()=>product.bridge.openFileDialog=originalChooser);}
 };
 await importFile(fixture+'/box.urdf');await record('import URDF');const urdf=states.at(-1).find(d=>!initial.some(a=>a.id===d.id));
 await importFile(fixture+'/arm/IR-1600.sdf');await record('import SDF');const sdf=states.at(-1).find(d=>!states.at(-2).some(a=>a.id===d.id));
 const toggle=page.locator(`[data-five-command="five.machine.definition-toggle:${sdf.id}"]`);
 await toggle.evaluate(el=>{window.statusNode=el;window.canvas=mv.viewport.renderer.domElement;window.panes=[...document.querySelectorAll('.cam-context-pane,.cam-info-pane')];panes.forEach(p=>p.scrollTop=20);window.scrolls=panes.map(p=>p.scrollTop);el.focus({preventScroll:true});});
 await toggle.click();await record('disable');
 assert.deepEqual(await toggle.evaluate(el=>({same:el===statusNode,focus:document.activeElement===el,canvas:canvas===mv.viewport.renderer.domElement,scroll:panes.every((p,i)=>p.scrollTop===scrolls[i])})),{same:true,focus:true,canvas:true,scroll:true});
 await toggle.click();await record('enable');
 const select=async id=>{await page.locator(`[data-five-command="five.machine.definition-select:${id}"]`).click();await page.waitForFunction(id=>mv.fiveMachine.definitionId===id,id);await settle();};
 await select(urdf.id);await page.locator('[data-command-id="five.machine-panel.delete"]').click();await record('delete URDF');
 for(let i=states.length-2;i>=0;i--){await undo(i===states.length-2);assert.deepEqual(await list(),states[i],'undo '+names[i]);assert.deepEqual(await sceneHistory(),sceneBefore);console.log('UNDO',names[i]);}
 assert.equal(await page.locator('[data-action="undo-project"]').isDisabled(),true);
 for(let i=1;i<states.length;i++){await redo();assert.deepEqual(await list(),states[i],'redo '+names[i-1]);console.log('REDO',names[i-1]);}
 // Disable/no-op/undo/new change invalidates only library redo.
 await toggle.click();await settle();const h=await history();
 await page.evaluate(id=>product.invoke('MachineDefinition.SetEnabled',{machineDefinitionId:id,enabled:false}),sdf.id);
 assert.deepEqual(await history(),h,'no-op enable must not add history');await undo();
 await select(sdf.id);await page.locator('[data-command-id="five.machine-panel.delete"]').click();await settle();assert.equal((await history()).canRedo,false);await undo();
 // Original source can disappear; restored managed model must still instantiate.
 renameSync(fixture+'/arm',fixture+'/arm-source-moved');assert.equal(existsSync(sdf.managedPath),true);
 await select(sdf.id);await page.locator('[data-command-id="five.machine-panel.instantiate"]').click();await settle();
 await page.waitForFunction(n=>mv.scene.machines.length===n,machinesBefore.length+1);await settle();
 assert.equal(await page.evaluate(()=>mv.scene.machines.length),machinesBefore.length+1);
 const instantiated=await page.evaluate(()=>mv.fiveMachine.machineId);assert.ok(instantiated);
 // Project history remains selected in instance mode and library history is untouched.
 const libraryBefore=await history();await undo();assert.deepEqual(await history(),libraryBefore);
 await page.waitForFunction(id=>!mv.scene.machines.some(m=>m.entityId===id),instantiated);
 assert.equal(await page.evaluate(id=>mv.scene.machines.some(m=>m.entityId===id),instantiated),false);await redo();
 await page.waitForFunction(id=>mv.scene.machines.some(m=>m.entityId===id),instantiated);await settle();
 await cmd('five.tool.machine-definitions');await settle();
 // Replacement imports preserve immutable files for both directions of history.
 const oldText=readFileSync(sdf.managedPath,'utf8');
 await page.evaluate(async({id,path})=>{await product.invoke('MachineDefinition.Import',{machineDefinitionId:id,sourcePath:path});const {refreshDefinitionHistory}=await import('../../../apps/five-axis-cam/webpage/machineDefinitionHistory.mjs');await refreshDefinitionHistory({productProxy:product});await __icaxAppShell.executeRibbonCommand('five.refresh');},{id:sdf.id,path:fixture+'/box.urdf'});await settle();
 const replacement=(await list()).find(d=>d.id===sdf.id);assert.notEqual(replacement.managedPath,sdf.managedPath);assert.equal(readFileSync(sdf.managedPath,'utf8'),oldText);
 await undo();assert.equal((await list()).find(d=>d.id===sdf.id).managedPath,sdf.managedPath);await redo();assert.equal((await list()).find(d=>d.id===sdf.id).managedPath,replacement.managedPath);await undo();
 await page.evaluate(path=>mv.viewportSceneProxy.invoke('Project.Save',{projectPath:path}),project);
 const final=await list();assert.deepEqual(errors,[]);await page.screenshot({path:out+'/definition-history.png'});
 writeFileSync(out+'/definition-history-report.json',JSON.stringify({pass:true,project,operations:names,sourceRemoved:true,instantiateRestored:true,replacementResources:true,sceneHistoryIsolated:true,domAndFocusPreserved:true,final},null,2));console.log('PASS native definition history');process.exit(0);
}catch(error){console.error(error);process.exit(1);}
