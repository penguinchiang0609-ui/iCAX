import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {readFileSync,writeFileSync} from 'node:fs';
import {chromium} from 'playwright';
const output=resolve('output/tests/placement-snapping'),base=pathToFileURL(resolve('output/tests/job-sequence/cef-runtime')+'/').href;
const expected=JSON.parse(readFileSync(resolve(output,'expected-state.json'),'utf8')),report=JSON.parse(readFileSync(resolve(output,'report.json'),'utf8'));
const browser=await chromium.connectOverCDP('http://127.0.0.1:9476'),page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');page.setDefaultTimeout(45000);
try{
 await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),expected.projectPath);
 await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId&&!__icaxAppShell.getState().pendingCount);
 await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
 await page.waitForFunction(()=>!fixture.view.pending&&fixture.view.scene?.domain?.fixtureUnits?.length===1);
 const state=JSON.parse(JSON.stringify(await page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}))));
 assert.deepEqual(state.placements,expected.state.placements);assert.deepEqual(state.fixtureUnits,expected.state.fixtureUnits);assert.deepEqual(state.fixtures,expected.state.fixtures);assert.deepEqual(state.workpieces,expected.state.workpieces);
 assert.equal(await page.evaluate(()=>fixture.view.viewport.getGridState().spacing),50);report.freshProcessReopen=true;report.cases.push('A fresh standalone process restores exact snapped workpiece and complete fixture transforms, and uses the actual rendered grid spacing');
}catch(error){report.passed=false;report.failure=error.stack;process.exitCode=1;}
finally{writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
