import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {readFileSync,readdirSync,statSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY);
const base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
const report=JSON.parse(readFileSync(resolve(output,'report.json'),'utf8'));
assert.equal(report.passed,true);
const files=readdirSync(output).filter(p=>/^生成交互验收-\d+\.i5cam$/.test(p)).sort((a,b)=>statSync(resolve(output,b)).mtimeMs-statSync(resolve(output,a)).mtimeMs);
const project=resolve(output,files[0]);
const browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');page.setDefaultTimeout(45000);
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(p=>__icaxAppShell.openProject(p,'icax.five-axis-cam'),project);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
 await page.waitForFunction(()=>!fixture.view.pending&&!__icaxAppShell.getState().pendingCount);
 await page.locator('[data-tab-id="workpiece"]').click();
 const state=await page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}));
 const units=state.fixtureUnits.filter(u=>u.setupId===state.activeSetupId);
 assert.equal(units.length,3);
 assert.equal(units.filter(u=>u.kind==='Generated').length,2);
 for(const unit of units.filter(u=>u.kind==='Generated')){
  assert.equal(unit.partIds.length,4);assert.ok(unit.design.breakdown);
  await page.locator('[data-five-command="five.preparation.unit:'+unit.id+'"]').click();
  await page.waitForFunction(()=>!fixture.view.pending&&!fixture.view.fixtureRequest);
  assert.equal(await page.locator('[data-five-key="unit-generation-info"]').count(),1);
  assert.equal(await page.locator('[data-five-command="five.fixture.design.export"]').isEnabled(),true);
 }
 assert.equal(await page.evaluate(()=>fixture.view.error),'');
 report.freshProcessReopen=true;report.savedProject=project;
 report.cases.push('A fresh application process with an empty user profile reopens the saved project with both generated units, their eight plates and independent breakdown/export state');
 await page.screenshot({path:resolve(output,'fresh-reopen.png')});
}catch(error){report.passed=false;report.reopenError=error.stack;process.exitCode=1;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
