import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/pillar-generate-20261102/cef');mkdirSync(output,{recursive:true});
const project=resolve(output,'Pillar-桌面生成-'+Date.now()+'.i5cam');copyFileSync(resolve('output/acceptance/pillar-generate-20261102/Pillar-用户现场复现.i5cam'),project);
const browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9674'),report={passed:false,standaloneCef:true,project,errors:[]};let page;
try{
 page=browser.contexts()[0].pages()[0];page.setDefaultTimeout(60000);page.on('pageerror',e=>report.errors.push(e.message));await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');assert.equal(await page.evaluate(()=>!!__icaxAppShell.getState().activeProjectPath),false);
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.pg=getProjectView(__icaxAppShell.getState().activeProjectId);return !!pg.scene?.domain&&!pg.pending;},null,{timeout:120000});
 await page.locator('[data-tab-id="toolpath"]').click();await page.getByRole('tab',{name:'空移刀路',exact:true}).click();
 const read=()=>page.evaluate(()=>pg.viewportSceneProxy.invoke('CamScene.ReadState',{})),before=await read();assert.equal(before.intentToolpaths.length,21);
 await page.locator('[data-command-id="five.tool.generate"]').click();await page.waitForFunction(()=>!pg.pending&&!pg.fiveGeneration?.running&&!pg.fiveRapidBatch?.running&&pg.scene.domain.setups.find(s=>s.id===pg.scene.domain.activeSetupId).generatedGeometry?.status==='Current',null,{timeout:180000});
 const after=await read(),geometry=after.setups.find(s=>s.id===after.activeSetupId).generatedGeometry;
 assert.equal(geometry.operations.length,21);assert.equal(geometry.transitions.length,20);assert.ok(geometry.transitions.every(t=>t.geometryStatus==='Generated'&&t.validation==='Unchecked'));
 assert.equal(await page.evaluate(()=>pg.error),'');assert.equal(await page.locator('[data-five-rapid-id]').count(),20);assert.ok((await page.locator('[data-five-rapid-id]').allTextContents()).every(text=>!text.includes('尚未生成')));
 assert.ok(await page.evaluate(()=>pg.fiveGeneratedPreview.group.children.filter(o=>o.userData.generatedRapid).length>=20));
 for(const key of ['workpieces','placements','intentToolpaths','fixtureUnits','fixtures'])assert.deepEqual(after[key],before[key],key+' changed unexpectedly');
 await page.screenshot({path:resolve(output,'Pillar-21条切割与20段空移.png')});
 await page.locator('[data-command-id="five.tool.settings"]').click();assert.equal(await page.locator('[data-five-tool="settings"] [data-five-command="five.rapid.save-settings"]').count(),0);await page.locator('[data-five-tool="settings"] [data-five-command="five.rapid.close"]').click();
 await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));report.cuts=21;report.generatedRapids=20;report.rapidValidation='Unchecked';report.sceneRapidLines=true;report.originalModelAndDefinitionsRetained=true;report.noSaveSettingsButton=true;assert.deepEqual(report.errors,[]);report.passed=true;
}catch(error){report.error=error.stack;if(page){report.state=await page.evaluate(()=>({error:pg?.error,pending:pg?.pending,task:pg?.fiveGeneration?.task})).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify(report));
