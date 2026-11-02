import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
const require=createRequire(import.meta.url),{chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out='D:/penguinchinage0609/iCAX/output/tests/machine-simulation',report=JSON.parse(readFileSync(out+'/definition-history-report.json','utf8'));
try {
 const browser=await chromium.connectOverCDP('http://127.0.0.1:9483'),page=browser.contexts()[0].pages()[0];
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),report.project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.mv=getProjectView(__icaxAppShell.getState().activeProjectId);window.product=mv.viewportSceneProxy.project.product;return !!mv.scene&&!mv.pending;});
 const current=await page.evaluate(async()=>({definitions:(await product.invoke('MachineDefinition.List',{})).definitions,history:await product.invoke('Product.GetSettingsHistory',{scope:'machineDefinitions'})}));
 assert.deepEqual(current.definitions,report.final);assert.equal(current.history.canUndo,false);assert.equal(current.history.canRedo,false);
 const sdf=report.final.findLast(d=>d.format==='SDF');
 const instance=await page.evaluate(id=>mv.viewportSceneProxy.invoke('Machine.Instantiate',{machineDefinitionId:id}),sdf.id);assert.ok(instance.machine);
 writeFileSync(out+'/definition-history-reopen.json',JSON.stringify({pass:true,catalogPreserved:true,restoredSdfInstantiates:true,sessionHistoryReset:true},null,2));
 console.log('PASS persisted definition catalog and managed SDF after restart');process.exit(0);
}catch(error){console.error(error);process.exit(1);}
