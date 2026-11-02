import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {chromium} from 'playwright';

const output=resolve(process.env.ICAX_BROWSER_REPORT_DIRECTORY);
const nativeReport=JSON.parse(readFileSync(resolve(output,'report.json'),'utf8'));
assert.equal(nativeReport.passed,true);
const base=pathToFileURL(resolve(process.env.ICAX_CEF_RUNTIME)+'/').href;
const browser=await chromium.connectOverCDP('http://127.0.0.1:'+process.env.ICAX_CEF_PORT);
const page=browser.contexts().flatMap(c=>c.pages()).find(p=>p.url()===base+'iCAX-UI/SDK/AppShell/index.html');
assert.ok(page);page.setDefaultTimeout(45000);
const errors=[],report={passed:false,project:nativeReport.project,cases:[],errors};
page.on('pageerror',error=>errors.push(error.message));
mkdirSync(resolve(output,'cef'),{recursive:true});
try{
  await page.waitForFunction(()=>window.__icaxAppShell?.getState().startupPhase==='ready');
  await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),nativeReport.project);
  await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
  await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
  await page.locator('[data-tab-id="workpiece"]').click();
  await page.waitForFunction(()=>!__icaxAppShell.getState().pendingCount&&!fixture.view.pending);
  const state=await page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));
  assert.equal(state.intentToolpaths.length,0);
  const unit=state.fixtureUnits.find(u=>u.id===nativeReport.unitId);assert.ok(unit);
  const part=unit.design.assembly.parts.find(p=>p.partId===nativeReport.partId);assert.ok(part.exactProfile);
  await page.locator(`[data-five-command="five.preparation.unit:${unit.id}"]`).click();
  await page.locator(`[data-five-command="five.fixture.part-select:${part.partId}"]`).click();
  await page.waitForFunction(id=>fixture.view.viewport.emphasizedObjectIds.has(id),part.partId);
  await page.evaluate(()=>{fixture.view.viewport.setViewDirection([1,-1,.8]);fixture.view.viewport.fitView();document.querySelector('.cam-info-pane').scrollTop=10000;});
  assert.equal(await page.getByText('验证光路',{exact:true}).count(),0);
  await page.screenshot({path:resolve(output,'cef','selected-part.png')});
  report.cases.push('Fresh standalone CEF reopens the edited real STEP project; row selection highlights its plate and no optical-validation button appears without paths');
  await page.locator(`[data-five-command="five.fixture.part-edit:${part.partId}"]`).click();
  await page.waitForFunction(()=>fixture.view.fiveFixturePlate?.profile&&!fixture.view.fiveFixturePlate.loading);
  assert.equal(await page.evaluate(()=>fixture.view.fiveFixturePlate.error),'');
  assert.deepEqual(await page.evaluate(()=>fixture.view.fiveFixturePlate.profile),part.exactProfile);
  await page.screenshot({path:resolve(output,'cef','reopened-editor.png')});
  await page.locator('[data-plate-action="close"]').last().click();
  assert.equal(await page.locator('[data-fixture-plate-editor]').count(),0);
  assert.deepEqual(errors,[]);
  report.cases.push('Reopening the 2D editor returns the exact saved profile and stable protected-boundary identities; cancel returns to the selected assembly');
  if(process.env.ICAX_FIXTURE_BREAKDOWN_RECHECK){
    const breakdown=async()=>{await page.locator('[data-five-command="five.fixture.design.breakdown"]').click();await page.getByRole('dialog',{name:'夹具板件清单'}).waitFor();assert.equal(await page.locator('.five-fixture-breakdown tbody tr').count(),unit.partIds.length);assert.equal(await page.locator('.five-fixture-breakdown tbody svg').count(),unit.partIds.length);};
    await breakdown();await page.locator('[data-five-command="five.fixture.design.breakdown-close"]').last().click();
    await page.evaluate(()=>__icaxAppShell.executeRibbonCommand('app.save'));
    await Promise.all([page.evaluate(()=>__icaxAppShell.closeProject(__icaxAppShell.getState().activeProjectId)),page.getByRole('button',{name:'保存并关闭',exact:true}).click()]);
    await page.evaluate(path=>__icaxAppShell.openProject(path,'icax.five-axis-cam'),nativeReport.project);
    await page.waitForFunction(()=>__icaxWorkbench.currentProjectId===__icaxAppShell.getState().activeProjectId);
    await page.evaluate(async base=>{const {getProjectView}=await import(base+'apps/_shared/workbench/state/projectViewStore.mjs');window.fixture={view:getProjectView(__icaxAppShell.getState().activeProjectId)};},base);
    await page.locator('[data-tab-id="workpiece"]').click();
    const reopened=await page.evaluate(()=>fixture.view.viewportSceneProxy.invoke('CamScene.ReadState',{}));const reopenedUnit=reopened.fixtureUnits.find(u=>u.id===nativeReport.unitId);
    assert.equal(reopenedUnit.design.assembly.geometryValidity,'Current');
    for(const part of reopenedUnit.design.assembly.parts)assert.equal(part.geometryRef.url,reopened.fixtures.find(f=>f.id===part.partId).geometryRef.url);
    await page.locator(`[data-five-command="five.preparation.unit:${unit.id}"]`).click();await breakdown();
    await page.screenshot({path:resolve(output,'cef','reopened-breakdown.png')});
    report.cases.push('Standalone CEF explicitly saves, closes and reopens the unique sample: every plate geometry reference matches its assembly, and the illustrated breakdown dialog opens successfully again');
  }
  report.passed=true;
}catch(error){report.error=error.stack;process.exitCode=1;await page.screenshot({path:resolve(output,'cef','failure.png')}).catch(()=>{});}
finally{writeFileSync(resolve(output,'cef','report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));await browser.close();}
