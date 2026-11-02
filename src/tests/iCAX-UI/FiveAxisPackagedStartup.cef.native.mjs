import assert from 'node:assert/strict';
import {chromium} from 'playwright';
import {copyFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY||'output/acceptance/startup-head-choice-20261102');mkdirSync(output,{recursive:true});
const runtime=resolve(process.env.ICAX_STARTUP_RUNTIME||'output/run/five-axis-cam-verified-startup-20261102').replaceAll('\\','/');
const source=resolve('output/acceptance/pillar-generate-20261102/Pillar-用户现场复现.i5cam'),project=resolve(output,'Pillar-启动验证-'+Date.now()+'.i5cam');copyFileSync(source,project);
const report={passed:false,runtime,project,errors:[],fileRequests:[]},browser=await chromium.connectOverCDP(process.env.ICAX_CDP_URL||'http://127.0.0.1:9678');let page;
try{
 page=browser.contexts()[0].pages().find(p=>p.url().includes('AppShell'));assert.ok(page);page.setDefaultTimeout(60000);page.on('pageerror',e=>report.errors.push(e.stack||e.message));page.on('request',r=>{if(r.url().startsWith('file:'))report.fileRequests.push(r.url());});
 assert.ok(decodeURI(page.url()).replaceAll('\\','/').includes(runtime+'/iCAX-UI/SDK/AppShell/'));
 await page.reload();await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');assert.equal(await page.evaluate(()=>__icaxAppShell.getState().error),'');assert.equal(await page.locator('.startup-screen:visible').count(),0);
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),project);await page.waitForFunction(async()=>{const {getProjectView}=await import('../../../apps/_shared/workbench/state/projectViewStore.mjs');window.startupVerified=getProjectView(__icaxAppShell.getState().activeProjectId);return !!startupVerified.scene?.domain&&!startupVerified.pending;},null,{timeout:120000});
 const domain=await page.evaluate(()=>startupVerified.viewportSceneProxy.invoke('CamScene.ReadState',{})),setup=domain.setups.find(s=>s.id===domain.activeSetupId);assert.equal(domain.intentToolpaths.length,21);assert.equal(setup.generatedGeometry.transitions.filter(t=>t.geometryStatus==='Generated').length,20);
 await page.locator('[data-tab-id="toolpath"]').click();await page.locator('[data-command-id="five.simulation.head-open"]').click();await page.waitForFunction(()=>startupVerified.fiveHeadPreview?.phase==='ready');
 assert.ok(await page.locator('[data-five-key="head-popover"]').isVisible());assert.equal(await page.evaluate(()=>startupVerified.error),'');assert.deepEqual(report.errors,[]);
 for(const name of ['cuttingHeadPreview.mjs','headChoicePicker.mjs'])assert.ok(report.fileRequests.some(url=>url.includes('/apps/five-axis-cam/webpage/'+name)),name+' must be served from the staged runtime');
 assert.ok(report.fileRequests.every(url=>decodeURI(url).replaceAll('\\','/').includes(runtime+'/')),'No frontend file may load from the source workspace');
 await page.locator('[data-five-command="five.simulation.head-close"]').click();await page.getByRole('tab',{name:'空移刀路',exact:true}).click();await page.screenshot({path:resolve(output,'启动成功-现场工程.png')});
 report.startupReady=true;report.mainWorkbench=true;report.cuttingHeadPreview=true;report.cuts=21;report.generatedRapids=20;report.onlyPackagedFrontend=true;report.passed=true;
}catch(error){report.error=error.stack;if(page){report.state=await page.evaluate(()=>window.__icaxAppShell?.getState()).catch(()=>null);await page.screenshot({path:resolve(output,'failure.png')}).catch(()=>{});}throw error;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));await browser.close();}
console.log(JSON.stringify({...report,fileRequests:report.fileRequests.length}));
