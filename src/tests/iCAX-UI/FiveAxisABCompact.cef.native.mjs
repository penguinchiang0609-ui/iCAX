import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {restoreNativeCefViewport} from './nativeCefViewport.mjs';
const output=resolve('output/acceptance/ab-compact-20261101/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,`紧凑AB头验证-${Date.now()}.i5cam`);copyFileSync(resolve('output/acceptance/head-instances-20261101/browser/刀头实例验证.i5cam'),project);
const report={passed:false,standaloneCef:true,project,errors:[]},browser=await chromium.connectOverCDP('http://127.0.0.1:9598');let page;
try{
 page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url().includes('AppShell'));assert.ok(page);page.on('pageerror',e=>report.errors.push(e.message));await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await restoreNativeCefViewport(page);
 assert.equal(await page.evaluate(()=>!!__icaxAppShell.getState().activeProjectPath),false);
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);
 await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.ab=getProjectView(__icaxAppShell.getState().activeProjectId);return !!ab?.scene?.domain&&!ab.pending;},null,{timeout:120000});
 const before=await page.evaluate(()=>ab.viewportSceneProxy.invoke('CamScene.ReadState',{}));await page.locator('[data-tab-id="toolpath"]').click();await page.locator('[data-command-id="five.simulation.head-open"]').click();
 await page.waitForFunction(()=>ab.fiveSimulation?.builtinHeads?.length);const heads=await page.evaluate(()=>ab.fiveSimulation.builtinHeads),select=page.locator('[data-five-simulation-field="builtin-head"]'),wait=()=>page.waitForFunction(()=>!ab.pending&&!ab.fiveSimulation.busy&&ab.fiveHeadPreview?.phase==='ready');
 await select.selectOption(heads.find(h=>h.kind==='AC').id);await wait();await select.selectOption(heads.find(h=>h.kind==='AB').id);await wait();
 const name=page.locator('[data-five-head-instance-field="name"]');await name.fill('AB · 紧凑侧置摆头');await name.press('Enter');await page.waitForFunction(()=>!ab.pending&&!ab.fiveHeadEditor?.busy);
 const head=await page.evaluate(()=>ab.scene.domain.setups.find(s=>s.id===ab.scene.domain.activeSetupId).headModel);assert.equal(head.assembly.nodes.length,3);
 const ranges=head.assembly.nodes.filter(n=>n.type==='revolute').map(n=>[n.joint,Math.round(n.lower*180/Math.PI),Math.round(n.upper*180/Math.PI)]);assert.deepEqual(ranges,[['B',-50,50],['A',-46,50]]);
 assert.equal(await page.locator('[data-five-head-range="1"]').innerText(),'范围：-50° ～ 50°');assert.equal(await page.locator('[data-five-head-range="2"]').innerText(),'范围：-46° ～ 50°');
 await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'紧凑AB刀头.png')});
 const engineering=await page.evaluate(()=>ab.viewportSceneProxy.invoke('CamScene.ReadState',{}));for(const key of ['workpieces','placements','intentToolpaths','fixtures','fixtureUnits'])assert.deepEqual(engineering[key],before[key]);
 for(const [index,value] of [[2,'30'],[1,'25']])await page.locator(`[data-five-head-joint="${index}"]`).evaluate((input,value)=>{input.value=value;input.dispatchEvent(new Event('input',{bubbles:true}));},value);await page.locator('[data-five-key="head-popover"]').screenshot({path:resolve(output,'AB组合摆动.png')});
 assert.deepEqual(await page.evaluate(()=>ab.viewportSceneProxy.invoke('CamScene.ReadState',{})),engineering);await page.locator('[data-five-head-joints-reset]').click();
 report.ranges=ranges;report.cadAndPathsUnchanged=true;report.previewDoesNotSaveAngles=true;assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page)await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});throw error;}finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
