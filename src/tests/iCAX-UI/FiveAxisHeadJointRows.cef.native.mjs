import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/head-joint-rows-20261102/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,'关节平铺验证.i5cam');copyFileSync(resolve(process.env.ICAX_HEAD_TEST_PROJECT||'output/acceptance/head-ui-redesign-20261101/browser/刀头实例验证.i5cam'),project);
const report={passed:false,standaloneCef:true,project,errors:[]},browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9660');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));assert.ok(page);page.setDefaultTimeout(30000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');assert.equal(await page.evaluate(()=>!!__icaxAppShell.getState().activeProjectPath),false,'Use an isolated clean runtime');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.jv=getProjectView(__icaxAppShell.getState().activeProjectId);return !!jv?.scene?.domain&&!jv.pending;},null,{timeout:120000});
 await page.locator('[data-tab-id="toolpath"]').click();await page.waitForFunction(()=>jv.fiveAxisArea==='toolpath'&&!jv.pending);await page.locator('[data-command-id="five.simulation.head-open"]').click();
 const combo=page.locator('[data-five-simulation-field="head-choice"]');await page.waitForFunction(()=>jv.fiveSimulation.builtinHeads?.length);
 const ac=await page.evaluate(()=>jv.fiveSimulation.builtinHeads.find(h=>h.kind==='AC'));await combo.selectOption('builtin:'+ac.id);
 await page.waitForFunction(()=>!jv.pending&&!jv.fiveSimulation.headChoiceBusy&&jv.fiveHeadPreview?.phase==='ready');
 const dialog=page.locator('[data-five-key="head-popover"]'),rows=dialog.locator('.five-head-preview-joint-row'),sliders=rows.locator('input[type="range"]');
 assert.equal(await rows.count(),2);assert.deepEqual(await rows.locator('button').allTextContents(),['C 试转','A 试转']);for(const row of await rows.all())assert.equal(await row.isVisible(),true);
 const read=()=>page.evaluate(()=>jv.viewportSceneProxy.invoke('CamScene.ReadState',{})),before=await read();
 await page.evaluate(()=>{window.jk={rows:[...document.querySelectorAll('.five-head-preview-joint-row')],canvas:jv.fiveHeadPreview.canvas,main:jv.viewport.renderer.domElement,model:jv.fiveHeadPreview.model,camera:jv.fiveHeadPreview.camera.position.toArray(),parts:jv.fiveHeadPreview.model.children.map(p=>p.matrix.toArray())};});
 await sliders.nth(0).fill('30');await sliders.nth(0).dispatchEvent('input');await sliders.nth(1).fill('20');await sliders.nth(1).dispatchEvent('input');
 assert.deepEqual(await sliders.evaluateAll(nodes=>nodes.map(n=>n.value)),['30','20']);assert.deepEqual(await rows.locator('output').allTextContents(),['30°','20°']);
 assert.notDeepEqual(await page.evaluate(()=>jv.fiveHeadPreview.model.children.map(p=>p.matrix.toArray())),await page.evaluate(()=>jk.parts));
 await rows.nth(1).locator('button').click();assert.deepEqual(await sliders.evaluateAll(nodes=>nodes.map(n=>n.value)),['30','20']);
 assert.equal(await page.evaluate(()=>jk.canvas===jv.fiveHeadPreview.canvas&&jk.main===jv.viewport.renderer.domElement&&jk.model===jv.fiveHeadPreview.model&&jk.rows.every((row,i)=>row===document.querySelectorAll('.five-head-preview-joint-row')[i])),true);
 assert.deepEqual(await page.evaluate(()=>jv.fiveHeadPreview.camera.position.toArray()),await page.evaluate(()=>jk.camera));assert.deepEqual(await read(),before);
 await dialog.screenshot({path:resolve(output,'AC关节同时平铺.png')});
 await dialog.locator('[data-five-head-joints-reset]').click();assert.deepEqual(await sliders.evaluateAll(nodes=>nodes.map(n=>n.value)),['0','0']);assert.deepEqual(await read(),before);
 assert.deepEqual(report.errors,[]);report.passed=true;report.joints=['C','A'];report.combinedAngles=[30,20];report.savedStateUnchanged=true;report.rowsCanvasAndCameraRetained=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
