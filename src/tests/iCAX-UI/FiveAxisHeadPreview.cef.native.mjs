import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,existsSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';

const output=resolve('output/acceptance/head-preview-20261101');mkdirSync(output,{recursive:true});
const source=resolve('output/projects/CAM-工作副本-20261101.i5cam'),project=resolve(output,`切割头预览验收-${Date.now()}.i5cam`);
copyFileSync(source,project);if(existsSync(source+'.log'))copyFileSync(source+'.log',project+'.log');
const report={passed:false,standaloneCef:true,realModel:true,heads:[],errors:[],project};
const browser=await chromium.connectOverCDP(process.env.ICAX_HEAD_PREVIEW_CDP||'http://127.0.0.1:9595');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));page.setDefaultTimeout(30000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await restoreNativeCefViewport(page);
 if(await page.evaluate(()=>!!__icaxAppShell.getState().activeProjectPath)){
  await page.evaluate(()=>{window.previousHeadProject=__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId);});
  const save=page.locator('[data-project-close-choice="save"]');await save.waitFor({timeout:1500}).catch(()=>{});if(await save.isVisible())await save.click();await page.evaluate(()=>previousHeadProject);
 }
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async path=>{const{getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.hp=getProjectView(__icaxAppShell.getState().activeProjectId);return __icaxAppShell.getState().activeProjectPath===path&&hp.scene?.intentToolpaths?.length===31&&!hp.pending;},project,{timeout:180000});
 await page.locator('[data-tab-id="toolpath"]').click();await page.locator('[data-command-id="five.simulation.head-open"]').click();
 await page.waitForFunction(()=>hp.fiveSimulation?.builtinHeads?.length===9);
 const read=()=>page.evaluate(()=>hp.viewportSceneProxy.invoke('CamScene.ReadState',{})),before=await read();
 const heads=await page.evaluate(()=>hp.fiveSimulation.builtinHeads),select=page.locator('[data-five-simulation-field="builtin-head"]');
 const mainCamera=await page.evaluate(()=>({position:hp.viewport.camera.position.toArray(),quaternion:hp.viewport.camera.quaternion.toArray()}));
 await page.evaluate(()=>{window.mainCanvas=hp.viewport.renderer.domElement;});
 for(const head of heads.filter(h=>h.kind)){
  await select.selectOption(head.id);await page.waitForFunction(id=>!hp.pending&&!hp.fiveSimulation?.busy&&!hp.fiveSimulation?.request&&!hp.fiveSimulation?.queuedBuiltinHeadId&&hp.fiveHeadPreview?.phase==='ready'&&hp.scene.domain.setups.find(s=>s.id===hp.scene.domain.activeSetupId).headModel.builtinDefinitionId===id,head.id,{timeout:60000});
  const facts=await page.evaluate(()=>{const s=hp.fiveHeadPreview;return{canvas:[s.canvas.width,s.canvas.height],parts:s.model.children.length,color:getComputedStyle(document.querySelector('.five-cam-head-dialog')).color};});assert.ok(facts.canvas.every(v=>v>0));assert.equal(facts.color,'rgb(32, 54, 64)');
  await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,head.kind.replace('+','plus')+'.png')});report.heads.push({id:head.id,kind:head.kind,...facts});
 }
 const canvas=page.locator('[data-five-head-preview]'),box=await canvas.boundingBox();await page.mouse.move(box.x+box.width*.45,box.y+box.height*.45);await page.mouse.down({button:'right'});await page.mouse.move(box.x+box.width*.7,box.y+box.height*.6,{steps:6});await page.mouse.up({button:'right'});await page.mouse.wheel(0,100);await canvas.press('f');
 assert.deepEqual(await page.evaluate(()=>({position:hp.viewport.camera.position.toArray(),quaternion:hp.viewport.camera.quaternion.toArray()})),mainCamera);assert.equal(await page.evaluate(()=>mainCanvas===hp.viewport.renderer.domElement),true);
 const after=await read();for(const key of ['workpieces','placements','fixtures','fixtureUnits','intentToolpaths'])assert.deepEqual(after[key],before[key],key+' changed');
 await page.locator('[data-five-command="five.simulation.head-close"]').click();assert.equal(await page.evaluate(()=>hp.fiveHeadPreview===null),true);
 await page.locator('[data-command-id="five.simulation.head-open"]').click();await page.waitForFunction(()=>hp.fiveHeadPreview?.phase==='ready');
 const ac=heads.find(h=>h.kind==='AC');await select.selectOption(ac.id);await page.waitForFunction(id=>!hp.pending&&!hp.fiveSimulation.busy&&hp.fiveHeadPreview?.phase==='ready'&&hp.scene.domain.setups.find(s=>s.id===hp.scene.domain.activeSetupId).headModel.builtinDefinitionId===id,ac.id,{timeout:60000});
 await page.screenshot({path:resolve(output,'新版切割头预览.png')});
 report.cadAndPathsUnchanged=true;report.mainCameraUnchanged=true;report.closeReopenPassed=true;assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}finally{
 if(page)report.nativeViewport=await restoreNativeCefViewport(page);writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();
}
console.log(JSON.stringify(report));
