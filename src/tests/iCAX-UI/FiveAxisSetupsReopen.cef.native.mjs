import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {readFileSync,writeFileSync} from 'node:fs';
const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY),runtime=resolve(process.env.ICAX_CEF_RUNTIME),base=pathToFileURL(runtime+'/').href;
const expected=JSON.parse(readFileSync(resolve(output,'expected-state.json'),'utf8')),report=JSON.parse(readFileSync(resolve(output,'report.json'),'utf8'));
const {chromium}=await import(process.env.ICAX_PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.connectOverCDP(`http://127.0.0.1:${process.env.ICAX_CEF_PORT||9451}`),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');assert.ok(page);page.setDefaultTimeout(45000);
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
 await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),expected.projectPath);
 await page.waitForFunction(()=>!!window.__icaxWorkbench?.currentProjectId&&__icaxAppShell.getState().pendingCount===0);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
 await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene?.domain?.placements.length===2);
 // JSON stores -0 as 0; normalize that representation before exact comparison.
 const state=JSON.parse(JSON.stringify(await page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}, {timeoutMs:120000}))));
 assert.deepEqual(state.placements,expected.state.placements);assert.deepEqual(state.workpieces,expected.state.workpieces);
 assert.deepEqual(state.setups.find(s=>s.id===expected.setupId).registrationPlan,expected.state.setups.find(s=>s.id===expected.setupId).registrationPlan);
 await page.locator('[data-tab-id="fixture"]').click();await page.waitForFunction(()=>!fixture.view.pending);
 await page.locator('[data-command-id="five.tool.translate"]').click();await page.waitForFunction(()=>!fixture.view.pending);await page.locator('.five-placement-tools').waitFor();
 await page.screenshot({path:resolve(output,'reopened-setups.png')});
 report.cases.push('Fresh standalone application process opens the saved project with both setup matrices, shared geometry and locating records intact, and restores the move gizmo');report.passed=true;delete report.error;
}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'reopen-failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
