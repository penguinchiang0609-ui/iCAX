import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
import {createUndoRedoAudit} from './FiveAxisUndoRedoAudit.cef.native.mjs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation/ui-history-20261023';
const restart=process.argv.includes('--restart-check');
const sceneOnly=process.argv.includes('--scene-only');
try{
const b=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=b.contexts()[0].pages()[0];
const cdp=await page.context().newCDPSession(page);await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:true});
await page.setViewportSize({width:1600,height:1000});
await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
const pose=JSON.parse(readFileSync(out+'/pose/expected.json','utf8').replace(/^\uFEFF/,''));
const boxes=JSON.parse(readFileSync(out+'/interaction-report.json','utf8'));
const catalog=sceneOnly?null:JSON.parse(readFileSync(out+'/definition-ui-report.json','utf8'));
if(!restart&&!process.argv.includes('--already-closed')){
 // The project was already explicitly saved through the native Save dialog;
 // definition edits persist through product settings, independently of this file.
 await page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount);
 const id=await page.evaluate(()=>__icaxAppShell.getState().activeProjectId);
 await page.locator(`[data-action="close-open-project"][data-project-id="${id}"]`).click();
 await page.waitForFunction(id=>__icaxAppShell.getState().activeProjectId!==id||!!document.querySelector('[data-project-close-choice="save"]'),id);
 if(await page.locator('[data-project-close-choice="save"]').isVisible())await page.locator('[data-project-close-choice="save"]').click();
 await page.waitForFunction(id=>__icaxAppShell.getState().activeProjectId!==id,id);
}
if(!await page.locator('[data-action="open-project"][data-project-path]').first().isVisible())
 await page.locator('[data-action="open-start-center"]').click();
const recent=page.locator('[data-action="open-project"][data-project-path]').filter({hasText:'acceptance.i5cam'});
assert.ok(await recent.count()>0);
const paths=await recent.evaluateAll(ns=>ns.map(n=>n.dataset.projectPath.replaceAll('\\','/')));
assert.ok(paths.every(p=>p===pose.project));await recent.first().click();
console.log('Opened recent project through UI');
await page.waitForFunction(async path=>{const app=__icaxAppShell.getState();if(app.activeProjectPath?.replaceAll('\\','/')!==path)return false;const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');const view=getProjectView(app.activeProjectId);if(!view?.scene||view.pending||view.viewportSceneProxy?.project.projectId!==app.activeProjectId)return false;window.mv=view;window.fixture={view};return true;},pose.project);
await page.evaluate(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.fixture={view:mv};});
// Check the main execution world explicitly; the CEF polling context may still
// expose the startup project while AppShell finishes replacing its view.
let loaded=false;
for(let attempt=0;attempt<100&&!loaded;attempt++){
 loaded=await page.evaluate(async({path,ids})=>{
  const app=__icaxAppShell.getState();if(app.activeProjectPath?.replaceAll('\\','/')!==path)return false;
  const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');
  const view=getProjectView(app.activeProjectId);if(!view?.scene||view.pending)return false;
  const result=await view.viewportSceneProxy.invoke('Machine.List',{});
  if(!ids.every(id=>result.machines.some(m=>m.entityId===id)))return false;
  window.mv=view;window.fixture={view};return true;
 },{path:pose.project,ids:[boxes.machineId,pose.machineId]});
 if(!loaded)await new Promise(resolve=>setTimeout(resolve,100));
}
assert.ok(loaded,'saved project has loaded in the main CEF execution world');
await page.locator('[data-tab-id="simulation"]').click();
await page.locator('.ribbon-command[data-command-id="five.tool.machine-instances"]').click();
const checks=[];
for(const expected of [{machineId:boxes.machineId,tip:boxes.tip,final:boxes.records.at(-1).after},pose]){
 await page.locator('[data-five-machine-instance]').selectOption(expected.machineId);await page.waitForFunction(()=>!mv.fiveMachine.busy);
 await page.locator(`[data-five-command="five.machine.part:${expected.tip}"]`).click();
 const audit=await createUndoRedoAudit(page,out,expected.machineId,expected.tip);await audit.settle();const actual=await audit.read();
 console.log('Checking reopened machine',expected.machineId);
 assert.deepEqual(JSON.parse(JSON.stringify(actual.native)),expected.final.native,'native persisted bodies and joints');
 assert.deepEqual(actual.viewport.world,expected.final.viewport.world,'reopened 3D part transform');
 // Untouched built-in colliders acquire session UI IDs; compare their persisted
 // geometry and enabled/name values, not those temporary selection identifiers.
 const semantic=bs=>JSON.parse(JSON.stringify(bs.map(({id,...box})=>box)));
 assert.deepEqual(semantic(actual.viewport.boxes),semantic(expected.final.viewport.boxes),'reopened inspector boxes');
 await page.screenshot({path:out+`/${restart?'restart':'reopen'}-${checks.length}.png`});
 checks.push({machineId:expected.machineId,tip:expected.tip,native:true,viewport:true,inspector:true});
}
const definitions=await page.evaluate(async()=>(await mv.viewportSceneProxy.project.product.invoke('MachineDefinition.List',{})).definitions);
if(catalog){
assert.deepEqual(definitions,catalog.final,'persisted catalog including deletion and disabled definition');
assert.equal(definitions.some(d=>d.id===catalog.urdfId),false);
assert.equal(definitions.find(d=>d.id===catalog.sdfId).enabled,false);
}
const histories=await page.evaluate(async()=>({scene:await mv.viewportSceneProxy.getUndoRedoState(),definitions:await mv.viewportSceneProxy.project.product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'})}));
assert.equal(histories.scene.canUndo,false);assert.equal(histories.scene.canRedo,false);
if(restart){assert.equal(histories.definitions.canUndo,false);assert.equal(histories.definitions.canRedo,false);}
writeFileSync(out+`/${sceneOnly?'scene-only-':''}${restart?'restart':'reopen'}-report.json`,JSON.stringify({pass:true,actualRecentProjectUI:true,checks,catalogPreserved:!!catalog,deletedDefinitionAbsent:!!catalog,disabledDefinitionPreserved:!!catalog,histories},null,2));
console.log('PASS',restart?'process restart':'close and reopen','actual UI + persisted state');process.exit();
}catch(e){console.error(e);process.exit(1);}
